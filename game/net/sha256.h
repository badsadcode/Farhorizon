#pragma once
// SHA-256, so a password never crosses the network as readable text. Tested against the published vectors.
#include <array>
#include <cstdint>
#include <string_view>

namespace fh::net {

using Digest = std::array<std::uint8_t, 32>;
Digest sha256(std::string_view data);

// What a client sends to prove it knows the password: a hash mixed with a number the server picked for this
// one connection, so overhearing it once is no use on a later connection.
Digest passwordProof(std::uint64_t nonce, std::string_view password);

}  // namespace fh::net
