# What we found in /usr/bin/blender

blender is a program for Linux, built for x86_64 (64-bit). It looks like it was made with Embedded Python. Its internal names were removed (stripped), so most functions will have generated names.

Evidence ev_1b999c2ee49aefee2c2e7c171855918d0eacd1bd09daa54403ff23f9f0558872.

```json
{
  "architecture": "x86_64",
  "bits": 64,
  "dotnet_arch": null,
  "entry_point": "0x1a58480",
  "format": "elf",
  "format_name": "ELF (Linux, Android, BSD)",
  "kind": "executable",
  "libraries": [
    "libtbb.so.12",
    "libtbbmalloc.so.2",
    "libtbbmalloc_proxy.so.2",
    "libceres.so.4",
    "libglog.so.2",
    "libgflags.so.2.2",
    "libpng16.so.16",
    "libshaderc_shared.so.1",
    "libjpeg.so.8",
    "libwebp.so.7",
    "libfreetype.so.6",
    "libvulkan.so.1",
    "libspnav.so.0",
    "libX11.so.6",
    "libXfixes.so.3",
    "libXi.so.6",
    "libxkbcommon.so.0",
    "libopenxr_loader.so.1",
    "libopenjp2.so.7",
    "libmanifold.so.3",
    "libAlembic.so.1.8",
    "libMaterialXFormat.so.1",
    "libMaterialXCore.so.1",
    "libpotrace.so.0",
    "libcycles_kernel_oneapi_aot.so",
    "libsycl.so.9",
    "libosdGPU.so.3.7.0",
    "libosdCPU.so.3.7.0",
    "libOpenImageDenoise.so.2",
    "libopenvdb.so.13.0",
    "libembree4.so.4",
    "libopenpgl.so.0"
  ],
  "next_steps": [
    {
      "tool": "core_overview_show",
      "why": "a one-page summary: sections, imports, counts"
    },
    {
      "tool": "native_functions_list",
      "why": "every function we found"
    },
    {
      "tool": "native_strings_find",
      "why": "find text the program shows or uses"
    },
    {
      "tool": "native_function_study",
      "why": "a full dossier on one function (start with main or the entry point)"
    },
    {
      "tool": "core_feature_find",
      "why": "follow a word you see in the app to the code behind it"
    },
    {
      "tool": "native_layout_show",
      "why": "segments, symbols, relocations and hardening"
    }
  ],
  "operating_system": "Linux",
  "plain_english": "blender is a program for Linux, built for x86_64 (64-bit). It looks like it was made with Embedded Python. Its internal names were removed (stripped), so most functions will have generated names.",
  "recommended_toolsets": [
    "core",
    "native",
    "formats"
  ],
  "stripped": true,
  "symbol_table_stripped": true,
  "toolchain_hints": [
    {
      "category": "runtime",
      "evidence": "Python runtime import",
      "id": "python",
      "label": "Embedded Python",
      "strength": "weak"
    },
    {
      "category": "compiler",
      "evidence": ".comment: mold 2.42.0 (441dc1d1fd3c72f41bc9d30f440431d34738def3; compatible with GNU ld); GCC: (GNU) 16.2.1 20260810",
      "id": "compiler",
      "label": "GCC",
      "strength": "strong"
    }
  ]
}
```

## Ghidra

Deep mode: Ghidra has analysed /usr/bin/blender (297 min), so decompiling it is quick.

## How it looks running

Starting it for a picture didn't work (Picturing a program's window works on Windows and macOS, not on this system yet.), so there's no picture of it running.
