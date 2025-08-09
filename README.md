# wpk
**I made wpk with the help of Jehovah! I thank him so much!**


**WPK** (Water Package) Is an Package manager for linux, where you can install packages.

### How to install WPK (From Source)

```bash
wget https://raw.githubusercontent.com/zer0users/wpk/refs/heads/main/source/wpk.c -O wpk.c
gcc -o wpk wpk.c -lcurl
sudo mv wpk /usr/local/bin/wpk
```

### How to install WPK (From Compiled Source)

```bash
wget https://github.com/zer0users/wpk/raw/refs/heads/main/compiled/wpk -O wpk
chmod +x wpk
sudo mv wpk /usr/local/bin/wpk
```

### How to use WPK

To install an Package:

```bash
wpk install (Package)
```

Example with love:

```bash
wpk install water
```

This installs water from official repositories.
