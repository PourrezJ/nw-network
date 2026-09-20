# nw-network C++20 port

This directory is the standalone C++20 conversion of the Rust src tree from
fix/warboard-stats-839-layout. It deliberately has no dependency on ReNewWorld.

The hand-written C++ runtime implements the protocol primitives and replication
behaviour. The large reflected surface is generated from the same canonical
network-schema.json plus the Rust state declarations. Hand-written
#[replicated_state] declarations take priority over stale schema field lists, so
source fixes such as GdeMetadata fields and replication groups are retained.

The build is fail-closed for exported ReplicatedState coverage: every state
exported by src/states/mod.rs must resolve to a typeIndex and a generated C++
class, otherwise generation exits non-zero.

Build commands:

    cmake -S cpp -B cpp/build -DNW_NETWORK_BUILD_TESTS=ON
    cmake --build cpp/build --config Release --parallel
    ctest --test-dir cpp/build -C Release --output-on-failure

GitHub Actions runs this on both Linux and Windows. No ReNewWorld source is read,
modified, linked, or required.
