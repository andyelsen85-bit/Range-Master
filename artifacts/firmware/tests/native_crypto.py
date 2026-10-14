"""Find real native mbedTLS for SDK-shim tests (Nix omits its .pc file)."""
from pathlib import Path
import shlex
import subprocess
import sys

def mbedcrypto_flags(directory):
    if len(sys.argv)>1:
        prefix=Path(sys.argv[1])
        if not (prefix/"include/mbedtls/gcm.h").is_file():
            raise RuntimeError("Pass the native mbedTLS installation prefix")
        return ["-I",str(prefix/"include"),"-L",str(prefix/"lib"),
                "-Wl,-rpath,"+str(prefix/"lib"),"-lmbedcrypto"]
    try:
        return shlex.split(subprocess.check_output(
            ["pkg-config","--cflags","--libs","mbedcrypto"],text=True,
            stderr=subprocess.DEVNULL))
    except subprocess.CalledProcessError:
        # Nix does not provide mbedcrypto.pc. Use a real SHA-256 HMAC provider,
        # not a deterministic fake; target builds still compile native mbedTLS.
        from test_fire_link import MD
        header=Path(directory)/"mbedtls/md.h"
        header.parent.mkdir(exist_ok=True);header.write_text(MD)
        return ["-I",str(directory),*shlex.split(subprocess.check_output(
            ["pkg-config","--cflags","--libs","openssl"],text=True))]
