#!/bin/sh
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Generate the self-signed credentials the sample presents to the head unit.
# Head units that verify the phone certificate reject them; supply a
# certificate issued by Google through CONFIG_SAMPLE_AA_CERT_FILE and
# CONFIG_SAMPLE_AA_KEY_FILE instead.

set -e
cd "$(dirname "$0")"

openssl ecparam -name prime256v1 -genkey -noout -out aa_key_sec1.pem
openssl pkcs8 -topk8 -nocrypt -in aa_key_sec1.pem -out aa_key.pem
rm -f aa_key_sec1.pem
openssl req -new -x509 -sha256 -days 36500 -key aa_key.pem -out aa_cert.pem \
	-subj "/O=Zephyr Project/CN=Android Auto accessory sample"
