#!/usr/bin/env python3
"""Package the native arm64 desktop engine with a small owned-folder launcher."""
import argparse,hashlib,json,plistlib,re,shutil,subprocess,tempfile,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(*args):return subprocess.check_output(list(map(str,args)),stderr=subprocess.STDOUT,text=True)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--engine',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 p.add_argument('--engine-revision',required=True);a=p.parse_args()
 if a.output.exists():raise ValueError('Output exists; choose a new directory')
 if run('xcrun','lipo','-archs',a.engine).strip()!='arm64':raise ValueError('Expected arm64 engine')
 build=run('xcrun','vtool','-show-build',a.engine)
 if 'platform MACOS' not in build:raise ValueError('Expected native macOS engine')
 original=sha(a.engine)
 a.output.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='117-mac-stage-',dir=a.output.parent) as temporary:
  stage=Path(temporary);app=stage/'117.app';mac=app/'Contents/MacOS';res=app/'Contents/Resources'
  mac.mkdir(parents=True);res.mkdir()
  shutil.copy2(a.engine,mac/'halo-host');(mac/'halo-host').chmod(0o755)
  run('xcrun','swiftc','-O','-parse-as-library','-target','arm64-apple-macos26.0',ROOT/'native/EngineMac/Launcher.swift','-o',mac/'Launcher')
  info={'CFBundleName':'117','CFBundleDisplayName':'117','CFBundleIdentifier':'com.zephyrstudios.mastercheef.macos','CFBundleExecutable':'Launcher','CFBundlePackageType':'APPL','CFBundleShortVersionString':'1.0.30','CFBundleVersion':'130.1','LSMinimumSystemVersion':'26.0','LSApplicationCategoryType':'public.app-category.games','NSHighResolutionCapable':True,'GCSupportsGameMode':True}
  iconset=stage/'117.iconset';iconset.mkdir()
  source=ROOT/'native/EnginePad/Resources/Assets.xcassets/AppIcon.appiconset/117-plasma.png'
  for points in [16,32,128,256,512]:
   for scale in [1,2]:
    suffix='@2x' if scale==2 else ''
    run('sips','-z',str(points*scale),str(points*scale),source,'--out',iconset/f'icon_{points}x{points}{suffix}.png')
  run('iconutil','-c','icns',iconset,'-o',res/'117.icns')
  info['CFBundleIconFile']='117'
  (app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
  shutil.copy2(ROOT/'LICENSE',res/'PortLicense.txt')
  shutil.copy2(ROOT/'native/EngineVision/Resources/ThirdPartyNotices.txt',res/'ThirdPartyNotices.txt')
  # The desktop app selects an external owned game folder. No game payload or registration is copied.
  for binary in mac.iterdir():
   load=run('xcrun','vtool','-show-build',binary)
   versions=re.findall(r'\bminos (\S+)',load)
   if not versions or any(tuple(map(int,v.split('.')))>(26,0,0) for v in versions):
    raise ValueError('Binary requires a newer OS than the declared macOS26.0 minimum')
   run('codesign','--force','--sign','-','--timestamp=none',binary)
  run('codesign','--force','--sign','-','--timestamp=none',app)
  run('codesign','--verify','--deep','--strict',app)
  a.output.mkdir();asset=a.output/'117-macOS-arm64-130.1-preview.zip';files=[]
  with zipfile.ZipFile(asset,'x',zipfile.ZIP_DEFLATED,compresslevel=6,strict_timestamps=False) as z:
   for f in sorted(app.rglob('*')):
    if f.is_symlink():raise ValueError('Unexpected symlink')
    if f.is_file():
     rel=f.relative_to(stage).as_posix();z.write(f,rel);files.append({'path':rel,'bytes':f.stat().st_size,'sha256':sha(f)})
  with zipfile.ZipFile(asset) as z:assert z.testzip() is None
  result={'platform':'macOS','architecture':'arm64','minimumOSVersion':'26.0','build':'130.1','engineSourceRevision':a.engine_revision,'packagingSourceRevision':run('git','-C',ROOT,'rev-parse','HEAD').strip(),'launcherSourceSHA256':sha(ROOT/'native/EngineMac/Launcher.swift'),'inputEngineSHA256':original,'signing':'ad-hoc; not notarized','retailGameDataIncluded':False,'registrationIncluded':False,'launchAndGameplayTested':False,'asset':{'name':asset.name,'bytes':asset.stat().st_size,'sha256':sha(asset)},'files':files}
  (a.output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');(a.output/'SHA256SUMS').write_text(sha(asset)+'  '+asset.name+'\n')
  print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
 assert sha(a.engine)==original
if __name__=='__main__':main()
