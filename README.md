# CS:S Wallhack for Linux

Leverages the built-in `r_drawothermodels` rendering mode to enable wallhack on any server.

## Usage

```bash
g++ css_wh.cpp -o css_wh
sudo ./css_wh
```

Program finds the memory address of the `r_drawothermodels` so you can use it in any server no matter the `sv_cheats` value.
