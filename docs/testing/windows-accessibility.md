# Windows UI Automation testing

The Windows integration suite exercises the UI Automation fragment attached to
each D3D11 surface. It checks the semantic tree, control types, name updates,
keyboard focus, Invoke, Value, RangeValue, Toggle, recursive removal, screen
coordinates at the current DPI, and repeated swap-chain resizing.

On a Windows build, run the focused check with:

```powershell
ctest --test-dir build -C Debug -R "win_accessibility_uia|win_surface_d3d11" --output-on-failure
```

For an interactive screen-reader check, build the UI Explorer from the sibling
UIKit project with the Materia Haxeon toolchain and run `uikit/tools/showcase.sh`. The
desktop Explorer publishes its resolved semantic tree to its D3D11 surface on
each frame, and routes UI Automation actions back through the NativeKit event
queue into the focused widget.

With Narrator or Accessibility Insights for Windows running, inspect the
Explorer surface and confirm that headings, buttons, tabs, text fields, sliders,
and toggles have the names and states shown by the UI. Invoke a button, change a
slider, and edit the filter field; each action should update the visible
Explorer state and remain available through the accessibility tree. Resize the
window and move it between displays with different scale settings, then check
that the reported element rectangles stay aligned with the rendered controls.

The CI resize test repeatedly rebuilds D3D11 targets and checks that frames
remain drawable. Hardware device removal cannot be forced reliably on hosted
Windows runners; the interactive stress check should also be run while changing
GPU/display configuration on a Windows machine when validating device-loss
handling.
