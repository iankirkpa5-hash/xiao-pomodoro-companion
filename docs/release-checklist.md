# Release Checklist

Use this checklist before tagging a new firmware release.

## Branch

```powershell
git checkout main
git pull
git checkout -b release/vX.Y.Z-name
```

## Version And Docs

- [ ] `APP_VERSION` updated in `src/version.h`
- [ ] `README.md` Version Milestones updated
- [ ] `README.md` Features updated if behavior changed
- [ ] `README.md` Interaction table updated if controls changed
- [ ] README screenshots / assets updated if the UI changed

## Build And Device Test

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload
```

- [ ] Build passes
- [ ] Upload passes
- [ ] Real device smoke test passes
- [ ] Settings screen shows the expected version
- [ ] Round Display KE switch is set to ON / KE for backlight control
- [ ] Core interactions still work:
  - [ ] Short tap starts / pauses / resumes
  - [ ] Long press enters Settings from the normal screen
  - [ ] Settings `Save` exits correctly
  - [ ] Settings `Reset` returns to the normal Pomodoro screen
  - [ ] Settings `Stats` clears `Done`
  - [ ] Settings `Bright` visibly changes backlight brightness
  - [ ] Idle screen still appears only after paused inactivity

## Commit, Tag, Push

```powershell
git status
git add .
git commit -m "release vX.Y.Z"
git tag vX.Y.Z-name
git checkout main
git merge release/vX.Y.Z-name
git branch -d release/vX.Y.Z-name
git push origin main
git push origin --tags
```

- [ ] Git working tree clean
- [ ] Git tag created
- [ ] `main` pushed
- [ ] tags pushed

## Notes

- Do not move existing tags unless the release process explicitly requires it.
- For bugfix releases, use patch versions such as `v1.4.1-idle-reset-fix`.
- Keep test-only timer changes out of commits.
