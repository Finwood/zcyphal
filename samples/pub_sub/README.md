# zcyphal pub/sub sample

Demonstrates Cyphal publish/subscribe over Zephyr CAN loopback on `native_sim`.

```bash
west build -b native_sim -t run $ZEPHYR_MODULES/zcyphal/samples/pub_sub
```

Expect alternating publish activity and `cy message received` log lines.
