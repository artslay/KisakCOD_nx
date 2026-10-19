# Mesa/NVK compiler memory-safety fix

This patch is for the **Mesa source checkout that builds the Switch SDK**, not the KisakCOD C++ renderer. The Mesa source is not vendored in this repository, and the existing `mesa-sdk/libvulkan.a` is a prebuilt binary; an ordinary app-only `make` cannot incorporate a Rust compiler-source change.

## What changes

- `LowerBoundedU32Array::as_slice/as_mut_slice` intentionally expose the full logical array across the `arr` and `last` fields. The old code formed the full slice from `&self.arr`, a reference to only one field. The patch derives the pointer from the complete `#[repr(C)]` object instead.
- A regression test exercises mutation of the encoded last element through the full mutable slice.
- NVK's compiler-flags revision is raised from 8 to 9 so the changed NAK compiler cannot reuse shader-cache entries generated with the previous compiler.

The crash snapshot showed damaged newlib heap metadata when NAK's `VecRegAllocator` was being destroyed. This source has an actual unsafe slice/provenance issue in the SSA-vector representation used by that allocator, so it is a credible memory-safety cause to fix. **The snapshot does not, by itself, prove that this was the only source of corruption; a Switch hardware run is still required.**

## Apply and rebuild

Use the same `mesa-switch` source checkout and build setup that produced this project's `mesa-sdk`. From the Mesa source root, first validate the patch and then apply it:

`git apply --check /c/Users/Marie/KisakCOD_nx/ports/switch/mesa-patches/0001-nak-lower-bounded-whole-object-slice.patch`

`git apply /c/Users/Marie/KisakCOD_nx/ports/switch/mesa-patches/0001-nak-lower-bounded-whole-object-slice.patch`

Rebuild Mesa with the existing Switch/MSYS2 build process and install the resulting updated SDK libraries into `C:\\Users\\Marie\\KisakCOD_nx\\mesa-sdk\\opt\\devkitpro\\portlibs\\switch\\lib`.

Then, from the KisakCOD repository root, run `make clean && make`. A clean rebuild is required here because the app Makefile does not list the external `libvulkan.a` as a link prerequisite, so a plain `make` may leave the old driver linked into the NRO.

No shader is blacklisted or skipped by this patch.
