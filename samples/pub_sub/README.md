# zcyphal pub/sub sample

Demonstrates Cyphal publish/subscribe over Zephyr CAN loopback on `native_sim`.

```bash
west build -b native_sim $ZEPHYR_MODULES/zcyphal/samples/pub_sub
west build -t run
```

Expect alternating publish activity and `rx 1 bytes` log lines.
