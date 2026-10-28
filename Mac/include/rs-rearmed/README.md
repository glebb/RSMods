# RS Re-ARMed runtime headers

From [RS Re-ARMed](https://github.com/bbelgg/rs-rearmed) 1.2.0 (`tools/ws7/recomp/runtime/rc.h`, `rc_rt.h`,
`rc_call.h`, `rc_hook.h`; `tools/ws7/recomp/overrides/rc_ov.h`), MIT licensed (LICENSE here). They declare the
runtime that the game executable exports and the mods library calls: the guest context, calls into translated
code, hook points. Update them together with the RS Re-ARMed version the mods need.
