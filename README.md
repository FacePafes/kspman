# kspman
KSP mod manager YOU manage

---

## what?
kspman is a lightweight, local first mod manager for Kerbal Space Program.

It installs mods from zip files or links and keeps a simple local index of what you installed.

---

## philosophy
- no background services  
- no hidden decisions  
- no automatic dependency resolution  
- no ecosystem lock in  
- no silent changes  

if something gets installed, you chose it.  
if something updates, you approved it.

---

## how it works
- install a mod (zip file or url)
- it is extracted and added to a local index
- the index tracks what files belong to what mod
- right click to enable/disable mods (renames to .disabled)
- uninstall uses the tracked file list for clean removal
- you choose when to replace mods with newer versions

---

## updates
as of v0.1 kspman does not automatically check for updates.

when you want to update a mod:
you manually install the new version via zip or url.

you decide:
- update everything
- update specific mods
- skip anything you don’t want to touch

nothing is automatic.

---

## uninstall
uninstall is based on tracked install data.

if it was installed by kspman, it can be removed cleanly.

if it wasn’t tracked, it is considered outside the system and kspman won’t touch it.

---

## what it is not
- not a full ecosystem manager like CKAN (nothing against CKAN)  
- not a dependency resolver  
- not a background updater  
- not a system-wide installer  

---

## intended use
kspman is meant for:
- mods not available through CKAN
- manual installs you still want tracked
- users who want full visibility and control over installs
- small, explicit mod sets

---

## design boundary
kspman does not assume global knowledge of your mod environment.

it only manages what you explicitly give it.

this can lead to missing or undetected dependencies in some mods. kspman will alert you when dependency information is missing or unclear, but it does not automatically resolve or fetch anything.

many mods are designed for CKAN-style dependency systems, so manual review may be required.

---

## why not CKAN?
nothing against CKAN.

CKAN is designed for automatic ecosystem management, dependency resolution, and large scale mod coordination.

kspman is not.

kspman exists for situations where:
- a mod is not in CKAN
- you want explicit control over every install
- you prefer visible, manual state over automatic resolution

different tools, different goals.

if ckan sounds like more your style definitely check it out [CKAN's releases](https://github.com/KSP-CKAN/CKAN/releases)

---

## license
MIT License

you are free to use, modify, and distribute this software, provided you include attribution and do not hold the author liable for any damage caused by misuse or incorrect mod installation.
