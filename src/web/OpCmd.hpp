// src/web/OpCmd.hpp — Cmd implementation that records into an OpStream
//
// WASM/web builds only (VOLCANO_WEB). The per-frame/pre-pass command
// contexts are the same object: ops land in record order and the JS
// interpreter partitions them (resource → compute → render) per §2
// ordering rules.
#pragma once

#include "OpStream.hpp"

namespace volcano::web {

class OpCmd final : public render::Cmd {
public:
    explicit OpCmd(OpStream& s) : s_(s) {}
    OpStream& stream() { return s_; }
private:
    OpStream& s_;
};

/// Downcast helper mirroring render::vkCmd().
[[nodiscard]] inline OpStream& ops(render::Cmd& cmd) {
    return static_cast<OpCmd&>(cmd).stream();
}

} // namespace volcano::web
