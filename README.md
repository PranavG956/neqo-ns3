# neqo-ns3

An ns-3 module that runs Mozilla's [neqo](https://github.com/mozilla/neqo) QUIC
implementation (Rust) inside ns-3 simulations, through a small Rust-to-C bridge.

Status: work in progress. Handshake and single-stream bulk transfer work.
Server certificates currently come from neqo's test fixture.

## Layout
- `bridge/`   Rust crate (`cdylib`) exposing neqo-transport through a C API
- `model/`    NeqoClientApp, NeqoServerApp
- `helper/`   NeqoClientHelper, NeqoServerHelper
- `examples/` neqo-basic, neqo-dumbbell, neqo-chain

## Setup (tested on Ubuntu under WSL2)

git clone https://gitlab.com/nsnam/ns-3-dev.git && cd ns-3-dev
git clone <this repo> contrib/neqo
git clone https://github.com/mozilla/neqo third-party/neqo
git -C third-party/neqo checkout <NEQO_COMMIT>
cd contrib/neqo/bridge && NSS_NO_PKG_CONFIG=1 cargo build # first build compiles NSS: 15-30 min
cd ../../.. && ./ns3 configure --enable-examples
./ns3 run neqo-basic
Requires neqo's NSS build tools (mercurial, gyp, ninja, clang, zlib) and Rust.
