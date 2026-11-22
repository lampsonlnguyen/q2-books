#!/usr/bin/env python3
"""Release gate for the CA bundle CrossPod ships as .crosspod/ca-bundle.pem.

Usage: check_ca_bundle.py BUNDLE.pem
Every certificate must parse, and the roots that sign most self-hosted Kavita
and KOReader servers must be present (Q2 Pod's own scrobble bundle lacks
several of them, which is why CrossPod carries its own).
"""
import re
import ssl
import sys
import tempfile

REQUIRED = [
    "ISRG Root X1",                        # Let's Encrypt
    "ISRG Root X2",
    "USERTrust RSA Certification Authority",  # Sectigo RSA, ZeroSSL
    "SSL.com Root Certification Authority RSA",  # one of Cloudflare's edge CAs
    "DigiCert Global Root G2",
    "Amazon Root CA 1",
    "GTS Root R1",                         # Google Trust Services, Cloudflare
]

path = sys.argv[1]
pem = open(path, encoding="ascii").read()
blocks = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S)
assert len(blocks) >= 100, f"only {len(blocks)} certificates in {path}"
context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
context.load_verify_locations(cafile=path)  # raises on any unparsable certificate
names = set()
for cert in context.get_ca_certs():
    for rdn in cert.get("subject", ()):
        for key, value in rdn:
            if key == "commonName":
                names.add(value)
missing = [name for name in REQUIRED if name not in names]
assert not missing, f"CA bundle lacks: {missing}"
print(f"CA bundle: {len(blocks)} roots, required roots present: PASS")
