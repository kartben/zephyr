west build -b native_sim_64 -p auto samples/subsys/settings  -- -DCONFIG_NVS=y -DCONFIG_SETTINGS_NVS=y && west build -t run

xxd -s 0x000fc000 build/flash.bin | less

west build -b native_sim_64 -p auto -T sample.psa.its.secure_storage.entropy_driver samples/psa/its && west build -t run

xxd -s 0x000fc000 build/flash.bin | less
