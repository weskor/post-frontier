#!/usr/bin/env bash
set -euo pipefail

fail() { printf 'post-frontier: %s\n' "$*" >&2; exit 1; }
for tool in uname curl sha256sum tar mktemp mkdir mv chmod rm; do
    command -v "$tool" >/dev/null 2>&1 || fail "Required tool missing: $tool (install it and retry)."
done
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || fail 'Linux x86_64 is required.'
[[ ${HOME:-} == /* ]] || fail 'HOME must be an absolute path.'
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
[[ $data_home == /* ]] || fail 'XDG_DATA_HOME must be an absolute path.'
version=0.1.0
asset=Post-Frontier-0.1.0-linux-x86_64.tar.gz
url=https://github.com/weskor/post-frontier/releases/download/v0.1.0
destination="$data_home/post-frontier/$version"
bin="$HOME/.local/bin"
stage=
launcher_stage=
cleanup() {
    [[ -z $stage ]] || rm -rf -- "$stage"
    [[ -z $launcher_stage ]] || rm -f -- "$launcher_stage"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p -- "$data_home/post-frontier"
stage=$(mktemp -d "$data_home/post-frontier/.install-XXXXXX")
for file in "$asset" SHA256SUMS; do
    curl --fail --show-error --silent --location --proto '=https' --proto-redir '=https' \
        --output "$stage/$file" "$url/$file"
done
# Select exactly this asset, never paths or other commands supplied by the manifest.
checksum=
while read -r hash name extra; do
    if [[ $name == "$asset" || $name == "*$asset" ]]; then
        [[ -z $checksum && -z $extra && $hash =~ ^[[:xdigit:]]{64}$ ]] || fail 'Invalid or duplicate asset checksum.'
        checksum=$hash
    fi
done < "$stage/SHA256SUMS"
[[ -n $checksum ]] || fail "SHA256SUMS does not contain $asset."
printf '%s  %s\n' "$checksum" "$stage/$asset" | sha256sum --check --status || fail 'Archive checksum mismatch; installation unchanged.'
mkdir -- "$stage/unpacked"
tar --extract --gzip --file "$stage/$asset" --directory "$stage/unpacked" --no-same-owner --no-same-permissions
# ./x package --playtest wraps the complete package in one commit-named folder.
shopt -s nullglob dotglob
roots=("$stage/unpacked/"*)
[[ ${#roots[@]} == 1 && -d ${roots[0]} && ! -L ${roots[0]} && ! -L ${roots[0]}/PLAYTEST.sh && -f ${roots[0]}/PLAYTEST.sh && -x ${roots[0]}/PLAYTEST.sh ]] || fail 'Archive must contain one package folder with regular executable PLAYTEST.sh (not a symlink).'
if [[ -e $destination || -L $destination ]]; then
    [[ ! -L $destination && -d $destination && ! -L $destination/PLAYTEST.sh && -f $destination/PLAYTEST.sh && -x $destination/PLAYTEST.sh ]] || fail "Existing path is not a playtest installation: $destination (left untouched)."
    printf 'Keeping existing installation and saves: %s\n' "$destination"
else
    mv -T -- "${roots[0]}" "$destination"
fi
mkdir -p -- "$bin"
[[ ! -d $bin/post-frontier ]] || fail "Launcher path is a directory: $bin/post-frontier"
launcher_stage=$(mktemp "$bin/.post-frontier-XXXXXX")
{
    printf '#!/usr/bin/env bash\n'
    printf 'exec %q "$@"\n' "$destination/PLAYTEST.sh"
} > "$launcher_stage"
chmod 755 "$launcher_stage"
mv -T -- "$launcher_stage" "$bin/post-frontier"
launcher_stage=
printf 'Installed v%s. Start Steam, then run: %s\n' "$version" "$bin/post-frontier"
printf 'If ~/.local/bin is on PATH, you can also run: post-frontier\n'
