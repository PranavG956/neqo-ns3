NSS_LIB=$(ls -dt ~/ns-3-dev/contrib/neqo/bridge/target/debug/build/nss-rs-*/out/dist/Release/lib | head -1)
NSPR_LIB=$(ls -dt ~/ns-3-dev/contrib/neqo/bridge/target/debug/build/nss-rs-*/out/dist/nspr/Release/dist/lib | head -1)
export LD_LIBRARY_PATH=$HOME/ns-3-dev/contrib/neqo/bridge/target/debug:$NSS_LIB:$NSPR_LIB
