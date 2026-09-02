.. _tee_api:

Trusted Execution Environment (TEE)
###################################

Overview
********

A Trusted Execution Environment (TEE) is a trusted operating system that runs in a secure execution
space isolated from the main operating system, such as the Secure World of Arm TrustZone, or on a
separate secure co-processor. Trusted Applications (TAs) hosted by the TEE implement the logic that
must stay separated from the Normal World, for example secure storage.

The Zephyr TEE API is the Normal World, or client, side of that split. It is modeled after the
GlobalPlatform TEE Client API: an application opens a session to a Trusted Application identified
by its UUID, invokes functions of that TA with a small number of typed parameters, and exchanges
bulk data through shared memory. The driver turns each call into the transport understood by the
TEE, for example a Secure Monitor Call (SMC) for OP-TEE, and serves the requests that the TEE sends
back to the Normal World while a call is in progress. Arm Cortex-M applications that rely on
Trusted Firmware-M use the separate integration described in :ref:`tfm`, not this driver API.

Key concepts include:

**TEE device and capabilities**
  Each TEE is a regular Zephyr device. :c:func:`tee_get_version` fills a
  :c:struct:`tee_version_info` with the implementation identifier and capabilities, including the
  generic flags :c:macro:`TEE_GEN_CAP_GP`, :c:macro:`TEE_GEN_CAP_PRIVILEGED`,
  :c:macro:`TEE_GEN_CAP_REG_MEM` and :c:macro:`TEE_GEN_CAP_MEMREF_NULL`.

**Sessions**
  A session is the context in which a Trusted Application serves requests from one client.
  :c:func:`tee_open_session` takes a :c:struct:`tee_open_session_arg` with the
  :c:macro:`TEE_UUID_LEN` byte UUID of the TA, the UUID of the client and the login method
  (:c:macro:`TEEC_LOGIN_PUBLIC` or another ``TEEC_LOGIN_*`` value), and returns a session
  identifier that is later passed to :c:func:`tee_invoke_func`, :c:func:`tee_cancel` and
  :c:func:`tee_close_session`.

**Function invocation and parameters**
  :c:func:`tee_invoke_func` runs the TA function selected by :c:member:`tee_invoke_func_arg.func`
  within an open session; the meaning of the function identifiers is defined by each TA. Values
  and buffers are exchanged through an array of :c:struct:`tee_param` entries.

**Shared memory**
  Memory referenced by a parameter must be made known to the TEE before the call. A
  :c:struct:`tee_shm` object describes such a region: :c:func:`tee_shm_alloc` allocates and
  registers a new buffer, :c:func:`tee_shm_register` registers a buffer owned by the application,
  and :c:func:`tee_shm_free` and :c:func:`tee_shm_unregister` undo the respective operation.

**Supplicant**
  While serving a call, the TEE can ask the Normal World for services of its own, such as
  allocating memory or reading the time. Requests the driver cannot answer by itself are queued for
  a supplicant, an application thread that retrieves them with :c:func:`tee_suppl_recv` and returns
  the result with :c:func:`tee_suppl_send`.

Parameters
**********

Each request passes an array of :c:struct:`tee_param` entries whose length is given by the
``num_param`` argument of the call. The :c:member:`tee_param.attr` field tells whether an entry
is unused (:c:macro:`TEE_PARAM_ATTR_TYPE_NONE`), carries values or references shared memory, and
whether it is an input, an output or both. The role of the ``a``, ``b`` and ``c`` members depends
on that type:

* For value parameters (:c:macro:`TEE_PARAM_ATTR_TYPE_VALUE_INPUT`,
  :c:macro:`TEE_PARAM_ATTR_TYPE_VALUE_OUTPUT` and :c:macro:`TEE_PARAM_ATTR_TYPE_VALUE_INOUT`),
  :c:member:`tee_param.a`, :c:member:`tee_param.b` and :c:member:`tee_param.c` are three 64-bit
  values exchanged with the TA as they are.
* For memory reference parameters (:c:macro:`TEE_PARAM_ATTR_TYPE_MEMREF_INPUT`,
  :c:macro:`TEE_PARAM_ATTR_TYPE_MEMREF_OUTPUT` and :c:macro:`TEE_PARAM_ATTR_TYPE_MEMREF_INOUT`),
  :c:member:`tee_param.a` is the offset of the referenced region into the shared memory object,
  :c:member:`tee_param.b` is its size in bytes and :c:member:`tee_param.c` identifies the shared
  memory object. To pass a whole object, set the offset to 0 and the size to the size of the
  object.

The driver converts the parameters returned by the TEE back into the same array, so output and
in/out values are read from the array after the call. The OP-TEE driver rejects a
``NULL`` parameter array with ``-EINVAL`` even when ``num_param`` is 0, so a request without
parameters passes a single entry of type :c:macro:`TEE_PARAM_ATTR_TYPE_NONE`.

Shared Memory
*************

:c:func:`tee_shm_alloc` allocates a buffer of the requested size from the kernel heap (see
:ref:`heap_v2`) and registers it with the TEE. The buffer is described by :c:member:`tee_shm.addr`
and :c:member:`tee_shm.size` of the returned object, and :c:func:`tee_shm_free` unregisters the
object and frees the buffer. :c:func:`tee_shm_register` instead registers a buffer that the
application already owns and must keep valid until :c:func:`tee_shm_unregister` is called. Both
variants go through the :c:func:`tee_add_shm` and :c:func:`tee_rm_shm` helpers, which record the
:c:macro:`TEE_SHM_ALLOC` and :c:macro:`TEE_SHM_REGISTER` flags in :c:member:`tee_shm.flags` and
call the registration operations of the driver, advertised by :c:macro:`TEE_GEN_CAP_REG_MEM`.

With the OP-TEE driver, the shared memory identifier placed in :c:member:`tee_param.c` of a memory
reference parameter is the address of the :c:struct:`tee_shm` object itself, as shown in the
example below.

Devicetree Configuration
************************

A TEE is described by a devicetree node whose ``compatible`` property selects the driver. The
OP-TEE driver binds to :dtcompatible:`linaro,optee-tz` and requires the ``method`` property, which
selects whether the driver calls the OP-TEE Trusted OS with the ``smc`` or the ``hvc`` instruction.
The node has no register or interrupt resources, as in this example adapted from the driver test:

.. code-block:: devicetree

   / {
       firmware {
           optee {
               compatible = "linaro,optee-tz";
               method = "smc";
               status = "okay";
           };
       };
   };

Applications get the device with :c:macro:`DEVICE_DT_GET_ONE` and the compatible of the driver,
for example ``DEVICE_DT_GET_ONE(linaro_optee_tz)``.

Typical Application Flow
************************

#. Enable :kconfig:option:`CONFIG_TEE` and the driver for the TEE in use, for example
   :kconfig:option:`CONFIG_OPTEE`, and size the kernel heap
   (:kconfig:option:`CONFIG_HEAP_MEM_POOL_SIZE`) for the shared memory that the application and
   the driver allocate.
#. Get the TEE device from devicetree and check it with :c:func:`device_is_ready`. Drivers probe
   the TEE during initialization, so the device is not ready when no compatible TEE is running.
   :c:func:`tee_get_version` then reports the capabilities of the TEE.
#. Allocate or register the shared memory needed for memory reference parameters.
#. Open a session to the Trusted Application with :c:func:`tee_open_session`.
#. Invoke TA functions with :c:func:`tee_invoke_func`, reading results from the output parameters
   and from :c:member:`tee_invoke_func_arg.ret`.
#. Close the session with :c:func:`tee_close_session` and release the shared memory.

Basic Operation
***************

The following example opens a session to a Trusted Application, passes it one value and one shared
buffer, and closes the session again. The UUID and the function identifier are defined by the TA.

.. code-block:: c
   :caption: Invoking a Trusted Application function with a value and a memory reference

   #include <string.h>
   #include <zephyr/device.h>
   #include <zephyr/drivers/tee.h>

   #define TA_CMD_PROCESS 0

   static const uint8_t ta_uuid[TEE_UUID_LEN] = { /* UUID of the Trusted Application */ };

   int process_with_ta(void)
   {
       const struct device *const tee = DEVICE_DT_GET_ONE(linaro_optee_tz);
       struct tee_open_session_arg sess_arg = { .clnt_login = TEEC_LOGIN_PUBLIC };
       struct tee_invoke_func_arg inv_arg = { .func = TA_CMD_PROCESS };
       struct tee_param no_param = { .attr = TEE_PARAM_ATTR_TYPE_NONE };
       struct tee_param params[2] = { 0 };
       struct tee_shm *shm;
       uint32_t session_id;
       int ret;

       if (!device_is_ready(tee)) {
           return -ENODEV;
       }

       /* Buffer shared with the TA, allocated from the kernel heap */
       ret = tee_shm_alloc(tee, 256, 0, &shm);
       if (ret < 0) {
           return ret;
       }

       memcpy(sess_arg.uuid, ta_uuid, sizeof(sess_arg.uuid));
       ret = tee_open_session(tee, &sess_arg, 1, &no_param, &session_id);
       if (ret == 0 && sess_arg.ret != TEEC_SUCCESS) {
           /* The TEE refused the session, sess_arg.ret_origin tells which layer did */
           ret = -EIO;
       }
       if (ret < 0) {
           goto free_shm;
       }

       /* Parameter 0: a value passed to the TA */
       params[0].attr = TEE_PARAM_ATTR_TYPE_VALUE_INPUT;
       params[0].a = 42;

       /* Parameter 1: the whole shared buffer, read and updated by the TA */
       params[1].attr = TEE_PARAM_ATTR_TYPE_MEMREF_INOUT;
       params[1].a = 0;
       params[1].b = shm->size;
       params[1].c = (uint64_t)(uintptr_t)shm;

       inv_arg.session = session_id;
       ret = tee_invoke_func(tee, &inv_arg, ARRAY_SIZE(params), params);
       if (ret == 0 && inv_arg.ret != TEEC_SUCCESS) {
           /* The TA failed, inv_arg.ret holds its TEEC_* result code */
           ret = -EIO;
       }
       /* On success, shm->addr holds the data written by the TA */

       tee_close_session(tee, session_id);
   free_shm:
       tee_shm_free(tee, shm);

       return ret;
   }

Error Handling
==============

The API functions return 0 when the request was delivered to the TEE and a negative ``errno`` value
when it was not: ``-ENOSYS`` when the driver does not implement the operation, ``-EINVAL`` for
invalid arguments and ``-ENOMEM`` when no memory is available. A return value of 0 does not mean
that the Trusted Application accepted the request: the TEE reports its own result in
:c:member:`tee_open_session_arg.ret` and :c:member:`tee_invoke_func_arg.ret` as one of the
``TEEC_*`` codes of the GlobalPlatform specification, such as :c:macro:`TEEC_SUCCESS`,
:c:macro:`TEEC_ERROR_BAD_PARAMETERS` or :c:macro:`TEEC_ERROR_TARGET_DEAD`, and the matching
``ret_origin`` member (:c:macro:`TEEC_ORIGIN_API`, :c:macro:`TEEC_ORIGIN_COMMS`,
:c:macro:`TEEC_ORIGIN_TEE` or :c:macro:`TEEC_ORIGIN_TRUSTED_APP`) tells which layer produced the
code. Applications must check both values.

Supplicant
**********

A TEE call is synchronous for the calling thread, but while the Secure World works on the request
it can issue remote procedure calls (RPC) back to the Normal World. The driver answers some of them
itself: the OP-TEE driver allocates and frees kernel shared memory for the TEE, reports the system
uptime, suspends the calling thread for a requested number of milliseconds and implements the
notification wait and send primitives that OP-TEE uses for synchronization. Any other request is
queued for the supplicant, an application thread dedicated to serving them.

:c:func:`tee_suppl_recv` blocks until the TEE issues a request, then returns the request identifier
and its parameters. The caller passes the size of its :c:struct:`tee_param` array in ``num_params``
and gets back the number of entries actually used; the call fails with ``-EINVAL`` when the array
is too small and with ``-EBUSY`` when a previous request has not been answered yet. After serving
the request, the supplicant calls :c:func:`tee_suppl_send` with the updated parameters and a
``TEEC_*`` result code, and the driver copies the output and in/out entries into the pending
request before resuming the call to the TEE. With OP-TEE, the request identifier is one of the
``OPTEE_RPC_CMD_*`` values defined in :zephyr_file:`drivers/tee/optee/optee_rpc_cmd.h`; shared
memory requested for the client application, for instance, arrives as ``OPTEE_RPC_CMD_SHM_ALLOC``
and the supplicant returns the address of the buffer it allocated in the ``c`` member of the value
parameter.

.. code-block:: c
   :caption: Skeleton of a supplicant thread

   static void supplicant_thread(void *p1, void *p2, void *p3)
   {
       const struct device *const tee = DEVICE_DT_GET_ONE(linaro_optee_tz);
       struct tee_param params[4];

       while (true) {
           unsigned int num_params = ARRAY_SIZE(params);
           uint32_t result = TEEC_ERROR_NOT_SUPPORTED;
           uint32_t func;

           if (tee_suppl_recv(tee, &func, &num_params, params) < 0) {
               break;
           }

           /*
            * Serve the requests the application supports, update the output
            * parameters and set result to TEEC_SUCCESS.
            */

           tee_suppl_send(tee, result, num_params, params);
       }
   }

Blocking and Cancellation
*************************

The TEE API functions must be called from thread context: the OP-TEE driver takes mutexes and
semaphores, allocates from the kernel heap and waits for the Secure World to return, so the
functions cannot be used from interrupt handlers. The OP-TEE driver reads the number of threads
available in OP-TEE during initialization and uses it to limit the calls in flight: a call issued
while all OP-TEE threads are busy waits until one of them completes. Several application threads
can therefore use the same device concurrently, up to that limit. Supplicant requests are served
one at a time.

A session or invocation request in progress can be interrupted from another thread with
:c:func:`tee_cancel`, which takes the session identifier and the cancellation identifier of the
request. The cancellation identifier is chosen by the application and stored in
:c:member:`tee_open_session_arg.cancel_id` or :c:member:`tee_invoke_func_arg.cancel_id` when the
request is issued. The TEE reports an operation that was canceled with :c:macro:`TEEC_ERROR_CANCEL`.
The OP-TEE driver sends a cancellation identifier only in the cancel message; it does not forward
the ``cancel_id`` of the open session and invoke arguments to OP-TEE.

OP-TEE Driver
*************

The OP-TEE driver in :zephyr_file:`drivers/tee/optee` connects Zephyr running in the Normal World of
Arm TrustZone to OP-TEE loaded as the secure payload (BL32 image). It is enabled with
:kconfig:option:`CONFIG_OPTEE`, which requires an ARM64 target built for the Normal World
(:kconfig:option:`CONFIG_ARMV8_A_NS`) with support for the SMC and HVC instructions
(:kconfig:option:`CONFIG_HAS_ARM_SMCCC`). The device is initialized at the ``POST_KERNEL`` level
with the :kconfig:option:`CONFIG_KERNEL_INIT_PRIORITY_DEVICE` priority; during initialization the
driver verifies the OP-TEE API UID, logs the OP-TEE revision, exchanges capabilities and reads the
number of secure threads.

Each request is translated into an OP-TEE message placed in a page-aligned buffer allocated from
the kernel heap, whose physical address is passed to OP-TEE with a Secure Monitor Call or a
Hypervisor Call, as selected by the ``method`` devicetree property. The driver repeats the call
until OP-TEE returns a final result, serving every RPC request raised in between. Shared memory is
described to OP-TEE as a list of 4 KiB physical pages, which requires dynamic shared memory support
in OP-TEE (``CFG_CORE_DYN_SHM``): registered buffers need not be physically contiguous, but their
virtual addresses must be translatable to physical addresses. :c:func:`tee_get_version` reports
the :c:macro:`TEE_GEN_CAP_GP` and :c:macro:`TEE_GEN_CAP_REG_MEM` capabilities. The notifications
that OP-TEE uses to synchronize with the Normal World are tracked in a bitmap sized by
:kconfig:option:`CONFIG_OPTEE_MAX_NOTIF`. RPC requests for I2C transfers are not implemented.

The driver test in :zephyr_file:`tests/drivers/tee/optee` runs on ``native_sim/native/64`` with a
mocked ``arm_smccc_smc()`` and shows the message sequences for sessions, shared memory, supplicant
requests and notifications.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_TEE`
* :kconfig:option:`CONFIG_OPTEE`
* :kconfig:option:`CONFIG_OPTEE_MAX_NOTIF`
* :kconfig:option:`CONFIG_HEAP_MEM_POOL_SIZE`

API Reference
*************

.. doxygengroup:: tee_interface
