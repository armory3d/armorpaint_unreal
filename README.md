# armorpaint_unreal
Unreal Engine plugin for ArmorPaint

- Download `armorpaint_unreal.zip` from [Releases](https://github.com/armory3d/armorpaint_unreal/releases)
- Extract `armorpaint_unreal.zip` into `your_unreal_project_path/Plugins` folder
- Restart Unreal Editor
- Select an object to paint on
  - Multiple selected objects are painted together and share one texture set, their UV maps should not overlap
- Click on the ArmorPaint icon in the toolbar
- Select folder where ArmorPaint is located during the first run
- Current materials of the object are baked into the first layer, the resolution is set at `Project Settings - Plugins - ArmorPaint - Bake Resolution`
- Paint and close ArmorPaint, choose to save changes
