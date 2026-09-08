#!/bin/sh
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Generate the self-signed credentials the head unit presents to the phone.
# A phone does not check the issuer, but it does reject a subject naming
# "CarService" or "Google Automotive Link". RSA is used because a phone
# negotiates an RSA key exchange.

set -e
cd "$(dirname "$0")"

openssl genrsa -out hu_key_pkcs1.pem 2048
openssl pkcs8 -topk8 -nocrypt -in hu_key_pkcs1.pem -out hu_key.pem
rm -f hu_key_pkcs1.pem
openssl req -new -x509 -sha256 -days 36500 -key hu_key.pem -out hu_cert.pem \
	-subj "/O=Zephyr Project/CN=Android Auto head unit sample"
