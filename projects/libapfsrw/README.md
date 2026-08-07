# libapfsrw

`libapfsrw` is a small userspace APFS image library. It intentionally mirrors
the APFS kext: 4096-byte blocks, unencrypted test containers, leaf root trees, and basic object-map lookup.

Current CLI:

```sh
apfsrw info image.apfs
apfsrw ls image.apfs
apfsrw cat image.apfs /hello.txt
```

Write support is exposed as API surface but returns `APFSRW_ENOTSUP` until the
kext's copy-on-write and spaceman allocation paths are moved over.
