# No Windows PC? Build the DLL in the cloud (free)

You do **not** need a Windows computer.  
GitHub gives free **Windows build machines** for public repos.

## What you do (about 10 minutes, once)

### 1. Free GitHub account
https://github.com/signup

### 2. New empty repository
- Click **New repository**
- Name e.g. `ToughnessFeast`
- Public is fine (and free Actions minutes are generous for this)
- **Do not** add README/license (keep it empty)

### 3. Push this project

On your Linux machine, in the extracted project folder (the one with `CMakeLists.txt`):

```bash
git init
git add .
git commit -m "ToughnessFeast RE_Kenshi plugin"
git branch -M main
git remote add origin https://github.com/YOUR_USER/ToughnessFeast.git
git push -u origin main
```

(Use your real username/repo URL. GitHub may ask you to log in.)

### 4. Wait for the build
1. Open the repo on github.com  
2. Click the **Actions** tab  
3. Open the run named **Build Windows DLL**  
4. Wait until it’s green (usually a few minutes)

### 5. Download the DLL
1. On the green run page, scroll to **Artifacts**  
2. Download **`ToughnessFeast-mod`**  
3. Unzip — you get:

```text
ToughnessFeast.dll
config.ini
RE_Kenshi.json
ToughnessFeast.mod
```

### 6. Install into Kenshi (Linux / Proton)
```text
~/.steam/steam/steamapps/common/Kenshi/mods/ToughnessFeast/
```
Put those four files in that folder. Enable the mod + RE_Kenshi in game.

---

## If Actions is disabled
Repo → **Settings → Actions → General → Allow all actions**

## If the build fails
Open the failed job log and copy the error. Common fixes:
- Re-run the workflow (LFS download glitch)
- Make sure you pushed the folder that contains `.github/workflows/build-windows-dll.yml`

## What this does *not* need
- A Windows PC  
- Visual Studio on your machine  
- CLion building the game DLL  

CLion on Linux is only for reading/editing source. The **cloud runner** produces the real DLL.
