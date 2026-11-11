#ifndef NEQO_NS3_BRIDGE_H
#define NEQO_NS3_BRIDGE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define NEQO_OUT_NONE 0

#define NEQO_OUT_DATAGRAM 1

#define NEQO_OUT_CALLBACK 2

#define NEQO_STATE_HANDSHAKING 0

#define NEQO_STATE_CONFIRMED 1

#define NEQO_STATE_CLOSED 2

#define NEQO_EV_NEW_STREAM 1

#define NEQO_EV_STREAM_READABLE 2

#define NEQO_EV_STREAM_WRITABLE 3

/**
 * Opaque to C++. C++ only ever holds a pointer to this.
 */
typedef struct NeqoBridgeConn NeqoBridgeConn;

typedef struct NeqoOutput {
  int32_t tag;
  /**
   * Datagram length in bytes (tag == DATAGRAM).
   */
  uintptr_t len;
  /**
   * Destination of the datagram (tag == DATAGRAM), host byte order.
   */
  uint32_t dst_ip;
  uint16_t dst_port;
  /**
   * Call process_output again after this many ns (tag == CALLBACK).
   */
  uint64_t callback_ns;
} NeqoOutput;

typedef struct NeqoEvent {
  int32_t tag;
  uint64_t stream_id;
} NeqoEvent;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct NeqoBridgeConn *neqo_bridge_client_new(uint32_t local_ip,
                                              uint16_t local_port,
                                              uint32_t remote_ip,
                                              uint16_t remote_port,
                                              uint64_t now_ns);

struct NeqoBridgeConn *neqo_bridge_server_new(void);

void neqo_bridge_free(struct NeqoBridgeConn *handle);

/**
 * Feed one received UDP payload into the connection. Returns 0 on success.
 */
int32_t neqo_bridge_process_input(struct NeqoBridgeConn *handle,
                                  const uint8_t *data,
                                  uintptr_t len,
                                  uint32_t src_ip,
                                  uint16_t src_port,
                                  uint32_t dst_ip,
                                  uint16_t dst_port,
                                  uint64_t now_ns);

/**
 * Ask the connection what to do next.
 * A datagram, if any, is copied into `buf` (capacity `cap`) and described in `out`.
 * Returns 0 on success, -1 on error, -2 if `buf` is too small.
 */
int32_t neqo_bridge_process_output(struct NeqoBridgeConn *handle,
                                   uint64_t now_ns,
                                   uint8_t *buf,
                                   uintptr_t cap,
                                   struct NeqoOutput *out);

int32_t neqo_bridge_state(struct NeqoBridgeConn *handle);

/**
 * Pop the next queued event into `out`. Returns 1 if an event was returned, 0 if none.
 */
int32_t neqo_bridge_next_event(struct NeqoBridgeConn *handle, struct NeqoEvent *out);

/**
 * Open a new bidirectional stream. Writes its id to `out_id`. Returns 0 on success.
 */
int32_t neqo_bridge_stream_create(struct NeqoBridgeConn *handle, uint64_t *out_id);

/**
 * Queue bytes on a stream. Returns the number of bytes accepted (can be less than
 * `len`, or 0 when flow control is full), or -1 on error.
 */
int64_t neqo_bridge_stream_send(struct NeqoBridgeConn *handle,
                                uint64_t stream_id,
                                const uint8_t *data,
                                uintptr_t len);

/**
 * Finish the sending side of a stream (sends FIN). Returns 0 on success.
 */
int32_t neqo_bridge_stream_close_send(struct NeqoBridgeConn *handle, uint64_t stream_id);

/**
 * Read received bytes from a stream into `buf`. Returns the number of bytes read
 * (0 = nothing available now), or -1 on error. `*fin` is set to 1 when the peer
 * finished the stream.
 */
int64_t neqo_bridge_stream_recv(struct NeqoBridgeConn *handle,
                                uint64_t stream_id,
                                uint8_t *buf,
                                uintptr_t cap,
                                int32_t *fin);

/**
 * Close the connection gracefully (queues a CONNECTION_CLOSE; call process_output
 * afterwards to flush it). Returns 0 on success.
 */
int32_t neqo_bridge_close(struct NeqoBridgeConn *handle, uint64_t now_ns, uint64_t app_error);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* NEQO_NS3_BRIDGE_H */
