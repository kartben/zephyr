.. _external_module_wolfssl:

wolfSSL
#######

Introduction
************

`wolfSSL`_ is a lightweight SSL/TLS library written in ANSI C and targeted for embedded, RTOS, and
resource-constrained environments. It provides cryptographic capabilities including SSL/TLS
protocols, wolfCrypt cryptographic library, and certificate management.

wolfSSL is optimized for embedded systems and provides industry-standard cryptographic protocols
while maintaining a small footprint and high performance.

wolfSSL is dual-licensed under both GPLv2 and commercial licenses.

Usage with Zephyr
*****************

The wolfSSL repository is a Zephyr :ref:`module <modules>` which provides SSL/TLS
and cryptographic capabilities to Zephyr applications. It includes wolfCrypt for
cryptographic operations and full SSL/TLS protocol support.

To pull in wolfSSL as a Zephyr module, add it as a West project in the ``west.yaml``
file:

.. code-block:: yaml

   manifest:
     projects:
       - name: wolfssl
         url: https://github.com/wolfSSL/wolfssl.git
         revision: master
         path: modules/crypto/wolfssl

After adding the project, run ``west update`` to fetch the module.

For more detailed instructions and API documentation, refer to xxx.

References
**********

.. target-notes::

.. _wolfSSL:
   https://github.com/wolfSSL/wolfssl
