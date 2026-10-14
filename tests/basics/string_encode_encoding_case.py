# Test that encoding names are matched case-insensitively like CPython.
# See https://github.com/adafruit/circuitpython/issues/11580

print(bytes("hi", "utf-8"))
print(bytes("hi", "UTF-8"))
print(bytes("hi", "Utf-8"))
print(bytes("hi", "utf8"))
print(bytes("hi", "UTF8"))
print("hi".encode("UTF-8"))
print(bytes("hi", "ascii"))
print(bytes("hi", "ASCII"))
try:
    bytes("hi", "latin-1")
except LookupError:
    print("LookupError")
