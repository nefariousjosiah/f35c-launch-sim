# Third-party assets

Third-party art is used by the project but is **not in this repository**. The folders
it goes into are ignored by git.

## Aircraft

The renders use a downloaded low-detail F-35B model (`lockheed-martin-f-35b-lightning-ii`,
a Blender file) as a stand-in. Its source and license are not recorded. Confirm both
before showing renders of it publicly.

`Tools/Blender/export_f35_parts.py` converts it for the rig:

- rescales it to the F-35C's 15.7 m length;
- removes the F-35B lift fan and the deployed ladder;
- groups the parts by what moves together;
- writes `rig.json` with the hinge axes, strut and nozzle positions.

The script expects that model's object names (`leftElevator`, `noseSuspension`,
`leftWheel`, ...). Another model needs the name table in `GROUPS` updated.

```bash
blender -b --disable-autoexec --factory-startup <model>.blend \
  --python Tools/Blender/export_f35_parts.py -- ThirdPartySource/F35B/export
UnrealEditor-Cmd.exe F35CLaunch.uproject -run=pythonscript -script=Tools/Editor/import_f35_model.py
```

Without the model, the launch actor falls back to a blockout airframe.

## Recommended upgrades

These are licensed under the Fab Standard License. It allows them in the project and
its renders and packaged builds, but forbids redistributing the files. Buy them with
your own Epic account.

| Asset | Creator | Used for | Price (Personal) | Get it | Install to |
|---|---|---|---|---|---|
| F-35C Lightning II | Saippua | High-detail hero aircraft (true C: folding wingtips, hook) | $49.99 | [Fab](https://www.fab.com/listings/30931b8b-411a-491e-bca2-85c663f5aded) | `Content/ThirdParty/Fab/F35C/` |
| USS CVN Supercarrier | EA3D | Carrier hull, island, elevators | $48.99 | [Fab](https://www.fab.com/listings/bcec4b4c-4a95-4985-8c2f-7df562687b39) | `Content/ThirdParty/Fab/Carrier/` |
| Waterline PRO 6 | Waterline | FFT open ocean | from $69.99 | [Fab](https://www.fab.com/listings/0c1fc983-db84-4df3-b623-03db76d552c6) | `Plugins/Marketplace/` |

Everything else is original work in this repository: the C++ code, the catapult deck
kit, the generated textures and materials, the effects and the level.
