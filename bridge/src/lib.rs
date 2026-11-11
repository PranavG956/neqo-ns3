//! C-ABI bridge between ns-3 (C++) and neqo-transport (Rust).
//! IPv4 only. Times are simulated nanoseconds supplied by the caller.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::rc::Rc;
use std::time::{Duration, Instant};

use neqo_common::{event::Provider, Datagram, Tos};
use neqo_transport::{
    Connection, ConnectionEvent, ConnectionParameters, Output, State, StreamId, StreamType,
};
use nss::AuthenticationStatus;
use test_fixture::{CountingConnectionIdGenerator, DEFAULT_ALPN, DEFAULT_SERVER_NAME};

// process_output result tags
pub const NEQO_OUT_NONE: i32 = 0;
pub const NEQO_OUT_DATAGRAM: i32 = 1;
pub const NEQO_OUT_CALLBACK: i32 = 2;

// connection states returned by neqo_bridge_state
pub const NEQO_STATE_HANDSHAKING: i32 = 0;
pub const NEQO_STATE_CONFIRMED: i32 = 1;
pub const NEQO_STATE_CLOSED: i32 = 2;

// event tags returned by neqo_bridge_next_event
pub const NEQO_EV_NEW_STREAM: i32 = 1;
pub const NEQO_EV_STREAM_READABLE: i32 = 2;
pub const NEQO_EV_STREAM_WRITABLE: i32 = 3;

/// Opaque to C++. C++ only ever holds a pointer to this.
pub struct NeqoBridgeConn {
    conn: Connection,
    /// Instant that corresponds to simulated time 0.
    base: Instant,
    events: VecDeque<NeqoEvent>,
}

#[repr(C)]
pub struct NeqoOutput {
    pub tag: i32,
    /// Datagram length in bytes (tag == DATAGRAM).
    pub len: usize,
    /// Destination of the datagram (tag == DATAGRAM), host byte order.
    pub dst_ip: u32,
    pub dst_port: u16,
    /// Call process_output again after this many ns (tag == CALLBACK).
    pub callback_ns: u64,
}

#[repr(C)]
pub struct NeqoEvent {
    pub tag: i32,
    pub stream_id: u64,
}

fn guard<T>(default: T, f: impl FnOnce() -> T) -> T {
    // A panic must never unwind into C++.
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(default)
}

fn addr(ip: u32, port: u16) -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::from(ip), port))
}

impl NeqoBridgeConn {
    fn new(conn: Connection, base: Instant) -> Self {
        Self { conn, base, events: VecDeque::new() }
    }

    fn at(&self, ns: u64) -> Instant {
        self.base + Duration::from_nanos(ns)
    }

    /// Consume neqo's events: answer certificate authentication here (using
    /// simulated time) and queue the stream events for C++.
    fn pump_events(&mut self, now_ns: u64) {
        let now = self.at(now_ns);
        let evs: Vec<ConnectionEvent> = self.conn.events().collect();
        for ev in evs {
            match ev {
                ConnectionEvent::AuthenticationNeeded => {
                    // The test fixture's certificates are trusted.
                    self.conn.authenticated(AuthenticationStatus::Ok, now);
                }
                ConnectionEvent::NewStream { stream_id } => self.events.push_back(NeqoEvent {
                    tag: NEQO_EV_NEW_STREAM,
                    stream_id: stream_id.as_u64(),
                }),
                ConnectionEvent::RecvStreamReadable { stream_id } => {
                    self.events.push_back(NeqoEvent {
                        tag: NEQO_EV_STREAM_READABLE,
                        stream_id: stream_id.as_u64(),
                    })
                }
                ConnectionEvent::SendStreamWritable { stream_id } => {
                    self.events.push_back(NeqoEvent {
                        tag: NEQO_EV_STREAM_WRITABLE,
                        stream_id: stream_id.as_u64(),
                    })
                }
                _ => {}
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn neqo_bridge_client_new(
    local_ip: u32,
    local_port: u16,
    remote_ip: u32,
    remote_port: u16,
    now_ns: u64,
) -> *mut NeqoBridgeConn {
    guard(std::ptr::null_mut(), || {
        let base = test_fixture::now(); // also initialises NSS
        let conn = Connection::new_client(
            DEFAULT_SERVER_NAME,
            DEFAULT_ALPN,
            Rc::new(RefCell::new(CountingConnectionIdGenerator::default())),
            addr(local_ip, local_port),
            addr(remote_ip, remote_port),
            ConnectionParameters::default(),
            base + Duration::from_nanos(now_ns),
        );
        match conn {
            Ok(conn) => Box::into_raw(Box::new(NeqoBridgeConn::new(conn, base))),
            Err(_) => std::ptr::null_mut(),
        }
    })
}

#[no_mangle]
pub extern "C" fn neqo_bridge_server_new() -> *mut NeqoBridgeConn {
    guard(std::ptr::null_mut(), || {
        let base = test_fixture::now();
        let conn = test_fixture::new_server::<CountingConnectionIdGenerator, &str>(
            DEFAULT_ALPN,
            ConnectionParameters::default(),
        );
        Box::into_raw(Box::new(NeqoBridgeConn::new(conn, base)))
    })
}

#[no_mangle]
pub extern "C" fn neqo_bridge_free(handle: *mut NeqoBridgeConn) {
    if !handle.is_null() {
        guard((), || unsafe { drop(Box::from_raw(handle)) });
    }
}

/// Feed one received UDP payload into the connection. Returns 0 on success.
#[no_mangle]
pub extern "C" fn neqo_bridge_process_input(
    handle: *mut NeqoBridgeConn,
    data: *const u8,
    len: usize,
    src_ip: u32,
    src_port: u16,
    dst_ip: u32,
    dst_port: u16,
    now_ns: u64,
) -> i32 {
    if handle.is_null() || data.is_null() || len == 0 {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        let bytes = unsafe { std::slice::from_raw_parts(data, len) }.to_vec();
        let dgram = Datagram::new(
            addr(src_ip, src_port),
            addr(dst_ip, dst_port),
            Tos::default(),
            bytes,
        );
        let now = b.at(now_ns);
        b.conn.process_input(dgram, now);
        b.pump_events(now_ns);
        0
    })
}

/// Ask the connection what to do next.
/// A datagram, if any, is copied into `buf` (capacity `cap`) and described in `out`.
/// Returns 0 on success, -1 on error, -2 if `buf` is too small.
#[no_mangle]
pub extern "C" fn neqo_bridge_process_output(
    handle: *mut NeqoBridgeConn,
    now_ns: u64,
    buf: *mut u8,
    cap: usize,
    out: *mut NeqoOutput,
) -> i32 {
    if handle.is_null() || out.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        let o = unsafe { &mut *out };
        let now = b.at(now_ns);

        b.pump_events(now_ns);

        *o = NeqoOutput { tag: NEQO_OUT_NONE, len: 0, dst_ip: 0, dst_port: 0, callback_ns: 0 };
        match b.conn.process_output(now) {
            Output::Datagram(d) => {
                let payload: &[u8] = &d;
                if payload.len() > cap || buf.is_null() {
                    return -2;
                }
                unsafe { std::ptr::copy_nonoverlapping(payload.as_ptr(), buf, payload.len()) };
                o.tag = NEQO_OUT_DATAGRAM;
                o.len = payload.len();
                match d.destination() {
                    SocketAddr::V4(a) => {
                        o.dst_ip = u32::from(*a.ip());
                        o.dst_port = a.port();
                    }
                    SocketAddr::V6(_) => return -1,
                }
            }
            Output::Callback(dur) => {
                o.tag = NEQO_OUT_CALLBACK;
                o.callback_ns = dur.as_nanos() as u64;
            }
            Output::None => {}
        }
        0
    })
}

#[no_mangle]
pub extern "C" fn neqo_bridge_state(handle: *mut NeqoBridgeConn) -> i32 {
    if handle.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &*handle };
        match b.conn.state() {
            State::Confirmed => NEQO_STATE_CONFIRMED,
            State::Closing { .. } | State::Draining { .. } | State::Closed(..) => NEQO_STATE_CLOSED,
            _ => NEQO_STATE_HANDSHAKING,
        }
    })
}

/// Pop the next queued event into `out`. Returns 1 if an event was returned, 0 if none.
#[no_mangle]
pub extern "C" fn neqo_bridge_next_event(handle: *mut NeqoBridgeConn, out: *mut NeqoEvent) -> i32 {
    if handle.is_null() || out.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        match b.events.pop_front() {
            Some(ev) => {
                unsafe { *out = ev };
                1
            }
            None => 0,
        }
    })
}

/// Open a new bidirectional stream. Writes its id to `out_id`. Returns 0 on success.
#[no_mangle]
pub extern "C" fn neqo_bridge_stream_create(handle: *mut NeqoBridgeConn, out_id: *mut u64) -> i32 {
    if handle.is_null() || out_id.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        match b.conn.stream_create(StreamType::BiDi) {
            Ok(id) => {
                unsafe { *out_id = id.as_u64() };
                0
            }
            Err(_) => -1,
        }
    })
}

/// Queue bytes on a stream. Returns the number of bytes accepted (can be less than
/// `len`, or 0 when flow control is full), or -1 on error.
#[no_mangle]
pub extern "C" fn neqo_bridge_stream_send(
    handle: *mut NeqoBridgeConn,
    stream_id: u64,
    data: *const u8,
    len: usize,
) -> i64 {
    if handle.is_null() || (data.is_null() && len > 0) {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        let slice: &[u8] = if len == 0 { &[] } else { unsafe { std::slice::from_raw_parts(data, len) } };
        match b.conn.stream_send(StreamId::new(stream_id), slice) {
            Ok(n) => n as i64,
            Err(_) => -1,
        }
    })
}

/// Finish the sending side of a stream (sends FIN). Returns 0 on success.
#[no_mangle]
pub extern "C" fn neqo_bridge_stream_close_send(handle: *mut NeqoBridgeConn, stream_id: u64) -> i32 {
    if handle.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        match b.conn.stream_close_send(StreamId::new(stream_id)) {
            Ok(()) => 0,
            Err(_) => -1,
        }
    })
}

/// Read received bytes from a stream into `buf`. Returns the number of bytes read
/// (0 = nothing available now), or -1 on error. `*fin` is set to 1 when the peer
/// finished the stream.
#[no_mangle]
pub extern "C" fn neqo_bridge_stream_recv(
    handle: *mut NeqoBridgeConn,
    stream_id: u64,
    buf: *mut u8,
    cap: usize,
    fin: *mut i32,
) -> i64 {
    if handle.is_null() || buf.is_null() || fin.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        let slice = unsafe { std::slice::from_raw_parts_mut(buf, cap) };
        match b.conn.stream_recv(StreamId::new(stream_id), slice) {
            Ok((n, f)) => {
                unsafe { *fin = i32::from(f) };
                n as i64
            }
            Err(_) => -1,
        }
    })
}

/// Close the connection gracefully (queues a CONNECTION_CLOSE; call process_output
/// afterwards to flush it). Returns 0 on success.
#[no_mangle]
pub extern "C" fn neqo_bridge_close(handle: *mut NeqoBridgeConn, now_ns: u64, app_error: u64) -> i32 {
    if handle.is_null() {
        return -1;
    }
    guard(-1, || {
        let b = unsafe { &mut *handle };
        let now = b.at(now_ns);
        b.conn.close(now, app_error, "kthxbye!");
        0
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const CLIENT: (u32, u16) = (0x0a00_0002, 5000); // 10.0.0.2:5000
    const SERVER: (u32, u16) = (0x0a00_0001, 4433); // 10.0.0.1:4433

    fn new_out() -> NeqoOutput {
        NeqoOutput { tag: 0, len: 0, dst_ip: 0, dst_port: 0, callback_ns: 0 }
    }

    /// Move datagrams from `from` to `to` until `from` has nothing more to send.
    fn shuttle(
        from: *mut NeqoBridgeConn,
        to: *mut NeqoBridgeConn,
        f: (u32, u16),
        t: (u32, u16),
        now: u64,
        buf: &mut [u8],
    ) -> usize {
        let mut n = 0;
        loop {
            let mut out = new_out();
            assert_eq!(neqo_bridge_process_output(from, now, buf.as_mut_ptr(), buf.len(), &mut out), 0);
            if out.tag != NEQO_OUT_DATAGRAM {
                return n;
            }
            n += 1;
            assert_eq!(
                neqo_bridge_process_input(to, buf.as_ptr(), out.len, f.0, f.1, t.0, t.1, now),
                0
            );
        }
    }

    #[test]
    fn handshake_through_c_api() {
        let client = neqo_bridge_client_new(CLIENT.0, CLIENT.1, SERVER.0, SERVER.1, 0);
        let server = neqo_bridge_server_new();
        assert!(!client.is_null() && !server.is_null());

        let mut buf = vec![0u8; 2048];
        let mut now: u64 = 0;
        let mut datagrams = 0;
        for _ in 0..5000 {
            datagrams += shuttle(client, server, CLIENT, SERVER, now, &mut buf);
            datagrams += shuttle(server, client, SERVER, CLIENT, now, &mut buf);
            now += 1_000_000;
            if neqo_bridge_state(client) == NEQO_STATE_CONFIRMED
                && neqo_bridge_state(server) == NEQO_STATE_CONFIRMED
            {
                break;
            }
        }
        println!("handshake finished after {datagrams} datagrams, {} ms simulated", now / 1_000_000);
        assert_eq!(neqo_bridge_state(client), NEQO_STATE_CONFIRMED);
        assert_eq!(neqo_bridge_state(server), NEQO_STATE_CONFIRMED);
        neqo_bridge_free(client);
        neqo_bridge_free(server);
    }

    #[test]
    fn stream_transfer_through_c_api() {
        let c = neqo_bridge_client_new(CLIENT.0, CLIENT.1, SERVER.0, SERVER.1, 0);
        let s = neqo_bridge_server_new();
        let mut buf = vec![0u8; 2048];
        let mut rbuf = vec![0u8; 65536];
        let total = 200_000usize;
        let payload = vec![7u8; total];

        let mut now: u64 = 0;
        let (mut sid, mut opened, mut sent, mut closed) = (0u64, false, 0usize, false);
        let (mut got, mut got_fin) = (0usize, false);

        for _ in 0..20_000 {
            shuttle(c, s, CLIENT, SERVER, now, &mut buf);
            shuttle(s, c, SERVER, CLIENT, now, &mut buf);

            if neqo_bridge_state(c) == NEQO_STATE_CONFIRMED {
                if !opened {
                    assert_eq!(neqo_bridge_stream_create(c, &mut sid), 0);
                    opened = true;
                }
                while sent < total {
                    let n = neqo_bridge_stream_send(c, sid, payload[sent..].as_ptr(), total - sent);
                    assert!(n >= 0);
                    if n == 0 {
                        break;
                    }
                    sent += n as usize;
                }
                if sent == total && !closed {
                    assert_eq!(neqo_bridge_stream_close_send(c, sid), 0);
                    closed = true;
                }
            }

            let mut ev = NeqoEvent { tag: 0, stream_id: 0 };
            while neqo_bridge_next_event(s, &mut ev) == 1 {
                if ev.tag == NEQO_EV_STREAM_READABLE {
                    loop {
                        let mut fin = 0;
                        let n = neqo_bridge_stream_recv(s, ev.stream_id, rbuf.as_mut_ptr(), rbuf.len(), &mut fin);
                        assert!(n >= 0);
                        got += n as usize;
                        if fin == 1 {
                            got_fin = true;
                            break;
                        }
                        if n == 0 {
                            break;
                        }
                    }
                }
            }
            now += 1_000_000;
            if got_fin {
                break;
            }
        }
        println!("server received {got} of {total} bytes, fin={got_fin}, {} ms simulated", now / 1_000_000);
        assert_eq!(got, total);
        assert!(got_fin);

        assert_eq!(neqo_bridge_close(c, now, 0), 0);
        shuttle(c, s, CLIENT, SERVER, now, &mut buf);
        neqo_bridge_free(c);
        neqo_bridge_free(s);
    }
}