# Mesa/NVK patch for dark or incorrect materials on Switch

This patch targets Mesa Switch/NVK builds based on `danfromtico/mesa-switch`
commit `1a8c1a66d6f` (Mesa 26.2.1). It fixes an SM50/Maxwell shader-compiler
bug: when an `FADD` has a long immediate operand and saturation enabled, the
compiler could select `FADD32I`, which has no saturation bit on SM50. The
`.SAT` operation could therefore be dropped. A separate Switch port reports
that `saturate(x - 0.4)` could become negative and render water black.

Patch file: `nvk-sm50-saturate.patch`

## Apply and rebuild Mesa

Run these commands from the root of the Mesa Switch source tree, where
`src/nouveau/compiler/nak/sm50.rs` exists:

```sh
git apply --check /path/to/KisakCOD_nx/patches/mesa-switch/nvk-sm50-saturate.patch
git apply /path/to/KisakCOD_nx/patches/mesa-switch/nvk-sm50-saturate.patch
```

Then rebuild the Mesa Switch SDK and replace the `libvulkan.a` used by
KisakCOD. KisakCOD's Switch `Makefile` links Vulkan statically with
`-lvulkan`, so changing the engine source alone cannot repair a compiler bug
inside an already-built Mesa archive. Rebuild the NRO after replacing the
archive.

The patch is intentionally limited to the known Maxwell saturation bug; it
does not include unrelated performance or instrumentation changes from other
ports. It is a targeted candidate for the dark-rendering failure, not proof
that every missing texture or lighting issue has the same cause. Confirm the
result on the console after rebuilding both Mesa and KisakCOD.
