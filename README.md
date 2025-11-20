# KISS Package Manager
Just another package manager.

## Installing KPM
### Linux
```bash
curl -sSL https://raw.githubusercontent.com/lPrimemaster/kpm/refs/heads/master/install/install.sh | bash
```
### Windows
```powershell
curl -sSL https://raw.githubusercontent.com/lPrimemaster/kpm/refs/heads/master/install/install.ps1 | powershell -NoProfile -ExecutionPolicy Bypass -
```

## Using KPM
### Installing packages
Any repo that contains a valid `kpm.yaml` file on the master branch and root directory
can be installed via the `kpm install` command.
```
kpm install <github_repo>

# Example
kpm install lPrimemaster/mulex-fk
```

### Removing packages
```
kpm remove <package>

# Example (from above)
kpm remove mulex-fk
```

## Packaging for KPM
Creating a package for KPM is subject to loads of changes, but for now the following is required:

1. Create a github release with the packaged files for the supported platforms and point it's name on kpm.yaml.
2. Release must be a `<name>.tar.gz` file. (might port to xz)
3. Ensure `kpm.yaml` is present on the master root directory.
4. Customize your `kpm.yaml`.
5. Profit?

## Example file
```yaml
# Taken from lPrimemaster/mulex-fk
metadata:
  name: mulex-fk
  description: The mulex-fk framework runtime and libraries.
  # version: $CMAKE:CMAKE_PROJECT_VERSION

dist:
  # Can be a directory or a url or a github repo
  # endpoint: http://localhost:9000 #! Testing
  endpoint: lPrimemaster/mulex-fk
  tag: latest
  packages:
    - windows_amd64: windows_amd64.tar.gz
    - linux_amd64: linux_amd64.tar.gz
    # - source: source.tar.gz 

    # Do not provide a source dist for now
    # Force the user to clone and install from source directly

  # This gets triggered when our package gets installed
  post_install:
    - exec: !linux
        cmd: python3 -c "import site;print(site.getusersitepackages())"
        output: PY_USITE
    - exec: !win32
        cmd: python -c "import site;print(site.getusersitepackages())"
        output: PY_USITE
    - mkdir: "!PY_USITE"
    - move : pymx/ !PY_USITE/pymx/
```

## Packaging notes

other available commands (self-explanatory):
`copy, rmdir, rmfile`

All the files created:
1. after install (from the tar.gz packages)
2. with post install commands (copy, move, mkdir)
will be deleted upon 'kpm remove <package>'

One can annotate post install commands with os specific tags.
The above example runs all the commands in order but skips commands that are not suitable for the OS in question.

## Disclaimer
This is just a proof of concept. WIP.
As of now I am using it to install most of my own software.
