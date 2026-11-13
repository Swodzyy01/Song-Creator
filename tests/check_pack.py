import base64, gzip, zlib, sys
ok = True
for name in ["edge_0", "edge_1", "edge_2", "edge_3", "level"]:
    raw = open(f"/tmp/{name}.raw", "rb").read()
    gz = base64.urlsafe_b64decode(open(f"/tmp/{name}.b64", "rb").read().decode())
    same = gzip.decompress(gz) == raw and zlib.decompress(gz, 16 + zlib.MAX_WBITS) == raw
    ok &= same
print("PACK ROUNDTRIP (vs python zlib):", "OK" if ok else "FAILED")
sys.exit(0 if ok else 1)
