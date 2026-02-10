# Release Guide

This guide explains how to create a new release of the ingame_overlay library.

## Automatic Release via Git Tags

The easiest way to create a release is by pushing a version tag:

1. Ensure all changes are committed and pushed to the main branch
2. Create and push a version tag:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. The GitHub Actions workflow will automatically:
   - Build the library for x64 and x86 architectures
   - Build hook debug versions for both architectures
   - Package all artifacts into ZIP files
   - Create a GitHub release with all the artifacts

## Manual Release via GitHub Actions

You can also trigger a release manually from the GitHub Actions UI:

1. Go to the **Actions** tab in the GitHub repository
2. Select the **Build and Release** workflow
3. Click **Run workflow**
4. Enter a tag name (e.g., `v1.0.0` or `v0.0.0-test` for testing)
5. Click **Run workflow**

The workflow will build and create a release with the specified tag name.

## Release Artifacts

Each release includes the following artifacts:

### Individual Packages
- **ingame_overlay_x64.zip** - x64 static library (.lib)
- **ingame_overlay_x86.zip** - x86 static library (.lib)
- **ingame_overlay_x64_hookdebug.zip** - x64 library with hook debugging (.lib + .pdb)
- **ingame_overlay_x86_hookdebug.zip** - x86 library with hook debugging (.lib + .pdb)
- **ingame_overlay_headers.zip** - Public header files

### Complete Package
- **ingame_overlay_complete.zip** - All of the above in one package

## Hook Debug Builds

The hook debug builds include:
- Detailed logging of hook installation and execution
- Stack traces for debugging crashes
- Output visible in DebugView or Visual Studio debugger

Use these builds when troubleshooting hooking issues or crashes.

## Version Numbering

Follow semantic versioning (semver) for version tags:
- **Major version** (v1.0.0 → v2.0.0): Breaking changes
- **Minor version** (v1.0.0 → v1.1.0): New features, backwards compatible
- **Patch version** (v1.0.0 → v1.0.1): Bug fixes, backwards compatible

## Testing Releases

To test the release workflow without creating an official release:
1. Use the manual workflow trigger
2. Use a test tag name like `v0.0.0-test` or `v0.0.0-alpha`
3. Verify the artifacts are built correctly
4. Delete the test release from the GitHub Releases page if needed
