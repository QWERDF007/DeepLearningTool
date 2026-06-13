# ui Module

## Scope

`ui` builds the `dltool_ui` target and exposes the `dltool.ui` QML module. It now keeps project-specific UI services only:

- `UILogger`
- `ProgressManager`
- `Utils`
- `SignalHelper`

Reusable controls, colors, fonts, icons, and dialog button enums are provided by `3rdparty/QuickUI` through the `quickui` QML module.

## Dependencies

- Business QML should import both `quickui` and `dltool.ui` when it needs QuickUI controls plus project services.
- C++ code that needs shared visual tokens should include QuickUI headers, for example `quickui/Color.h`.
- `tool/main.cpp` adds the build-tree QuickUI QML import root so `import quickui` works from local builds.

## Boundaries

- Do not add reusable visual controls to this module; add them to `3rdparty/QuickUI`.
- Keep this module free of business state such as projects, datasets, labels, and models.
- Background-thread updates to logs or progress should still return to the UI thread through Qt queued connections or the existing service APIs.
