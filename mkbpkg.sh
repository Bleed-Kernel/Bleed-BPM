#!/bin/sh
set -e

usage() {
    cat <<EOF
usage: mkbpkg.sh -n <name> -v <version> [options] <folder>

  -n name       package name, a-z 0-9 - _ . +
  -v version    any text, 1.0, 153.6.0esr
  -d text       one line description
  -e programs   the programs in it, space separated, bpm links them into /usr/local/bin
                (default: name, if its in the folder)
  -i PKGINFO    start from this PKGINFO, its other keys are kept (-n -v -d -e still win)
  -o file       where the package goes (default: ./<name>-<version>.bpkg)
  -1 .. -9      gzip level (default 9)

everything in <folder> installs into <drive>/opt/<name>/ on bleed. symlinks are followed
EOF
    exit 2
}

die() {
    echo "mkbpkg: $*" >&2
    exit 1
}

name= version= desc= exec= info= out= level=9
while getopts "n:v:d:e:i:o:123456789h" opt; do
    case $opt in
        n) name=$OPTARG ;;
        v) version=$OPTARG ;;
        d) desc=$OPTARG ;;
        e) exec=$OPTARG ;;
        i) info=$OPTARG ;;
        o) out=$OPTARG ;;
        [1-9]) level=$opt ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || usage
src=$1
[ -d "$src" ] || die "$src isnt a folder"

# keys from an existing PKGINFO fill in whatever wasnt given
key() {
    [ -n "$info" ] && sed -n "s/^$1[[:space:]]*=[[:space:]]*//p" "$info" | head -n 1
}
if [ -n "$info" ]; then
    [ -f "$info" ] || die "$info doesnt exist"
    [ -n "$name" ]    || name=$(key name)
    [ -n "$version" ] || version=$(key version)
    [ -n "$desc" ]    || desc=$(key description)
    [ -n "$exec" ]    || exec=$(key exec)
fi

[ -n "$name" ]    || die "no name, use -n"
[ -n "$version" ] || die "no version, use -v"
echo "$name" | grep -Eq '^[a-z0-9][a-z0-9._+-]{0,62}$' || die "'$name' isnt a usable name (a-z 0-9 - _ . +, starting with a letter or digit)"
case $version in *"
"*) die "the version cant have a newline in it" ;; esac
[ -n "$exec" ] || { [ -f "$src/$name" ] && exec=$name; }
for e in $exec; do
    [ -f "$src/$e" ] || die "-e $e but $src/$e isnt there"
done
[ -n "$out" ] || out=./$name-$version.bpkg

# sizes the way bpm counts them, bytes of every file once links are followed
files=$(find -L "$src" -type f | wc -l | tr -d ' ')
size=$(find -L "$src" -type f -exec wc -c {} + | awk '$2 != "total" || NF > 2 { s += $1 } END { printf "%d", s }')
[ "$files" -gt 0 ] || die "$src has no files in it"

stage=$(mktemp -d "${TMPDIR:-/tmp}/mkbpkg.XXXXXX")
trap 'rm -rf "$stage" "$out.part"' EXIT INT TERM

{
    echo "# bleed package, made by mkbpkg.sh"
    echo "name=$name"
    echo "version=$version"
    [ -z "$desc" ] || echo "description=$desc"
    [ -z "$exec" ] || echo "exec=$exec"
    echo "size=$size"
    echo "files=$files"
    echo "arch=x86_64"
    echo "built=$(date -u +%Y-%m-%d)"
    # keys only the given PKGINFO knows about
    if [ -n "$info" ]; then
        grep -Ev '^[[:space:]]*(#|$)|^(name|version|description|exec|size|files|arch|built)[[:space:]]*=' "$info" || true
    fi
} > "$stage/PKGINFO"

ln -s "$(cd "$src" && pwd)" "$stage/files"

# no mac metadata or owner names in it
if tar --version 2>/dev/null | grep -q bsdtar; then
    set -- --uid 0 --gid 0 --uname root --gname root --no-mac-metadata --no-xattrs --no-acls
else
    set -- --owner=root:0 --group=root:0 --no-xattrs --no-acls --sort=name
fi

echo "[BPKG] $name $version, $files files, $(( (size + 1048575) / 1048576 )) MB"
{ (cd "$stage" && COPYFILE_DISABLE=1 tar --format=pax -h "$@" -cf - PKGINFO files) || touch "$stage/failed"; } |
    gzip -n -$level > "$out.part"
[ ! -e "$stage/failed" ] || die "tar failed"
mv "$out.part" "$out"

if command -v shasum > /dev/null 2>&1; then
    sum=$(shasum -a 256 "$out" | cut -d' ' -f1)
else
    sum=$(sha256sum "$out" | cut -d' ' -f1)
fi
echo "[BPKG] $out ($(( ($(wc -c < "$out") + 1048575) / 1048576 )) MB) sha256 $sum"
