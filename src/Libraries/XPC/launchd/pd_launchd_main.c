/*
 * PureDarwin: launchd's actual entry point (main()) is real Apple
 * launchd.c, pulled into the link via -force_load on XPC_launchd_static.
 * This translation unit intentionally defines nothing - it exists only
 * because add_darwin_executable() requires at least one target_sources()
 * entry of its own.
 */
