# GraphCalc 3DS

A Desmos-style graphing calculator for the Nintendo 3DS (homebrew, `.3dsx`).
Top screen = graph. Bottom screen = expression list + touch keypad.

## Build
Install devkitPro with the `3ds-dev` group, then:

    export DEVKITPRO=/opt/devkitpro
    make

Copy `graphcalc.3dsx` to `/3ds/` on the SD card and launch from the Homebrew Launcher
(or `3dslink graphcalc.3dsx`, or load in Citra/Azahar).

## What you can type
| Kind | Example | Result |
|------|---------|--------|
| Function | `y=sin(x)`, `x^2`, `y=1/x` | plotted curve (bare `x` expressions plot as y=) |
| Implicit | `x^2+y^2=9`, `x=2`, `y=x*y` | marching-squares curve |
| Slider | `a=2` | constant; L/R adjusts it live. Unknown letters auto-create sliders |
| Point | `(1,2)`, `(a,2a)` | dot (moves with sliders) |
| Calculator | `2(3+4)^2/7` | shows `=result` in the list row |

Functions: sin cos tan asin acos atan sinh cosh tanh sec csc cot ln log (base 10)
exp sqrt cbrt abs floor ceil round sign fact min(a,b) max(a,b) mod(a,b); constants pi, e.
Implicit multiplication works (`2x`, `3(x+1)`, `xsin(x)`), but write `a*sin(x)` rather than `asin(x)`, which is arcsin.

## Controls
- **Touch list**: tap a row to edit (tap in text to place cursor), tap the coloured circle to show/hide, tap X to delete.
- **Touch keypad**: FN/123 switches pages, NEW adds an expression, CLR clears, `<` `>` move the cursor.
- **D-pad / Circle Pad**: pan. **A / B**: zoom in / out. **X**: reset view.
- **Y**: trace the selected function (D-pad left/right moves the point, shows coordinates).
- **L / R**: decrease / increase the selected slider (hold to speed up).
- **SELECT**: open the system keyboard for the selected expression.
- **START**: exit.

## Not implemented (vs. real Desmos)
Inequalities/shading, tables, regression, statistics, lists, parametric/polar curves,
derivative/integral notation, function definitions like `f(x)=...`, folders, notes,
intersection/zero finding, 3D, saving graphs. Letters render in uppercase (5x7 font).
