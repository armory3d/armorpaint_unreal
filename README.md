# armorpaint_unreal
Unreal Engine plugin for ArmorPaint

- Download `armorpaint_unreal.zip` from [Releases](https://github.com/armory3d/armorpaint_unreal/releases)
- Extract `armorpaint_unreal.zip` into `your_unreal_project_path/Plugins` folder
- Restart Unreal Editor
- Select an object to paint on
- Click on the ArmorPaint icon in the toolbar
- Select folder where ArmorPaint is located during the first run
- Paint and close ArmorPaint, choose to save changes
- Painted project is stored at `your_unreal_project_path/ArmorPaint/<Object>/<Object>.arm`
- Painted textures are imported into `Content/ArmorPaint/<Object>` and applied to the object through the `M_<Object>` material
- Click on the ArmorPaint icon again to continue painting the saved project
