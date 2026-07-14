# Field fun tools — OBS · GIMP · GPU · box (rewritten)

All **C++**, tiny ELFs, fit **Field product ISO** and **GitHub release cap**.

| Command | Binary | Replaces | Notes |
|---------|--------|----------|--------|
| `obs` | field-obs | OBS Studio | cams · snap · rec · local only |
| `gimp` | field-gimp | GIMP | PPM new/invert/flip/gray/chips/ascii |
| `nvtop` | field-nvtop | nvtop | AMOURANTHRTX · AMD/NVIDIA/Intel |
| `top` `ls` … | fieldbox | coreutils bits | multicall |

```bash
field-obs cams
field-obs snap /dev/video0 shot.ppm
field-gimp new 640 480 canvas.ppm
field-gimp chips canvas.ppm field.ppm
field-gimp ascii field.ppm 60
nvtop
```

**Not shipping:** Electron OBS, full GIMP/GTK, LibreOffice — those blow the GitHub ISO budget.

Product: `make product` → ~18 MiB Field boot (+ tools in host install / overlay).

God Bless.
