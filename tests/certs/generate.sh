#!/bin/bash
# Generate test certificates for TLS validation tests.
# Run from the tests/certs/ directory.
set -e
cd "$(dirname "$0")"

EXTFILE=$(mktemp)
trap "rm -f $EXTFILE" EXIT

echo "Generating test CA..."
openssl req -x509 -newkey rsa:2048 -keyout ca-key.pem -out ca-cert.pem \
    -days 3650 -nodes -subj "/CN=WoW Test CA" 2>/dev/null

echo "Generating valid cert (localhost)..."
openssl req -newkey rsa:2048 -keyout valid-key.pem -out valid.csr \
    -nodes -subj "/CN=localhost" 2>/dev/null
echo "subjectAltName=DNS:localhost,IP:127.0.0.1" > "$EXTFILE"
openssl x509 -req -in valid.csr -CA ca-cert.pem -CAkey ca-key.pem \
    -CAcreateserial -out valid-cert.pem -days 3650 \
    -extfile "$EXTFILE" 2>/dev/null

echo "Generating expired cert..."
openssl req -newkey rsa:2048 -keyout expired-key.pem -out expired.csr \
    -nodes -subj "/CN=localhost" 2>/dev/null
echo "subjectAltName=DNS:localhost,IP:127.0.0.1" > "$EXTFILE"
# Cert valid from 2020-01-01 to 2020-01-02 (long expired)
openssl x509 -req -in expired.csr -CA ca-cert.pem -CAkey ca-key.pem \
    -CAcreateserial -out expired-cert.pem \
    -not_before 20200101000000Z -not_after 20200102000000Z \
    -extfile "$EXTFILE" 2>/dev/null

echo "Generating wrong-host cert (badhost.example.com)..."
openssl req -newkey rsa:2048 -keyout wrong-host-key.pem -out wrong-host.csr \
    -nodes -subj "/CN=badhost.example.com" 2>/dev/null
echo "subjectAltName=DNS:badhost.example.com" > "$EXTFILE"
openssl x509 -req -in wrong-host.csr -CA ca-cert.pem -CAkey ca-key.pem \
    -CAcreateserial -out wrong-host-cert.pem -days 3650 \
    -extfile "$EXTFILE" 2>/dev/null

echo "Generating self-signed cert (not signed by CA)..."
openssl req -x509 -newkey rsa:2048 -keyout self-signed-key.pem \
    -out self-signed-cert.pem -days 3650 -nodes \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" 2>/dev/null

# Clean up CSRs and serial
rm -f *.csr *.srl

echo "Done. Generated certs:"
ls -1 *.pem
