#!/usr/bin/env python3
"""Ed25519 (RFC 8032) in plain Python, for the update signatures (doc/modern-ux/DESIGN-updates.md 3.2).

No packages: the release runner and the owner's Mac need nothing installed. Slow (a few milliseconds a
signature) and not constant-time, which is fine for signing six files on a CI runner; never use it to
verify untrusted input in a server. The app verifies with Monocypher (source/elektron/md/mdmmUpdate).

  python3 scripts/release/mdmm_ed25519.py --self-test     runs the RFC 8032 test vectors
"""

import hashlib
import sys

P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = (-121665 * pow(121666, P - 2, P)) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _sha512(data):
    return hashlib.sha512(data).digest()


def _add(a, b):
    # extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, x*y = T/Z
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    aa = (y1 - x1) * (y2 - x2) % P
    bb = (y1 + x1) * (y2 + x2) % P
    cc = 2 * t1 * t2 * D % P
    dd = 2 * z1 * z2 % P
    e, f, g, h = bb - aa, dd - cc, dd + cc, bb + aa
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s, pt):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = _add(q, pt)
        pt = _add(pt, pt)
        s >>= 1
    return q


def _recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * pow(5, P - 2, P) % P
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % P)


def _compress(pt):
    zinv = pow(pt[2], P - 2, P)
    x = pt[0] * zinv % P
    y = pt[1] * zinv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def _equal(a, b):
    return (a[0] * b[2] - b[0] * a[2]) % P == 0 and (a[1] * b[2] - b[1] * a[2]) % P == 0


def _expand(seed):
    if len(seed) != 32:
        raise ValueError("an ed25519 private key is 32 bytes")
    h = _sha512(seed)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    a, _ = _expand(seed)
    return _compress(_mul(a, G))


def sign(seed, message):
    a, prefix = _expand(seed)
    pub = _compress(_mul(a, G))
    r = int.from_bytes(_sha512(prefix + message), "little") % L
    rs = _compress(_mul(r, G))
    h = int.from_bytes(_sha512(rs + pub + message), "little") % L
    s = (r + h * a) % L
    return rs + int.to_bytes(s, 32, "little")


def verify(pub, message, signature):
    if len(pub) != 32 or len(signature) != 64:
        return False
    a = _decompress(pub)
    r = _decompress(signature[:32])
    if a is None or r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    h = int.from_bytes(_sha512(signature[:32] + pub + message), "little") % L
    return _equal(_mul(s, G), _add(r, _mul(h, a)))


# The update statement the app checks (DESIGN-updates.md 3.2): four lines, each ending in "\n".
def update_message(name, version, sha256_hex):
    return ("mdmm-update-v1\n%s\n%s\n%s\n" % (name, version, sha256_hex.lower())).encode("utf-8")


# RFC 8032 section 7.1, tests 1, 2 and 3: (secret key, public key, message, signature), hex.
RFC8032_VECTORS = [
    ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
     "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
    ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
     "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
    ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
     "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"),
]


def self_test():
    for sk, pk, msg, sig in RFC8032_VECTORS:
        seed, msg_b = bytes.fromhex(sk), bytes.fromhex(msg)
        assert public_key(seed).hex() == pk, "public key of " + sk
        assert sign(seed, msg_b).hex() == sig, "signature of " + sk
        assert verify(bytes.fromhex(pk), msg_b, bytes.fromhex(sig)), "verify " + sk
        bad = bytearray(bytes.fromhex(sig))
        bad[0] ^= 1
        assert not verify(bytes.fromhex(pk), msg_b, bytes(bad)), "a flipped bit verified"
        assert not verify(bytes.fromhex(pk), msg_b + b"x", bytes.fromhex(sig)), "another message verified"
    print("mdmm_ed25519: RFC 8032 vectors OK")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    print(__doc__)
    sys.exit(2)
