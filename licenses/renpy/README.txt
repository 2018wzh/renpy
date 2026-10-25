Ren'Py SDK runtime in ShinkiEMU

This core contains free software, including components under the GNU Lesser General Public License version 2.1 or later. The combined librenpython.dll incorporates LGPL components, including FFmpeg and FriBidi; an outer DLL boundary does not turn those components into permissively licensed software. The copied Python library includes chardet (LGPL) and certifi (MPL-2.0).

Keep LICENSE-RenPy.txt and the component notices in this directory, including python/chardet/LICENSE and python/certifi/LICENSE. Font licenses remain beside the fonts in Resources/renpy/common. See https://www.renpy.org/doc/html/license.html for the upstream component inventory.

Corresponding source and relinking material accompany each release as shinki-emu-renpy-<version>-<platform>-sources.zip, beside the binary at https://github.com/Project-Shinkirou/ShinkiEMU/releases . Unpack source.tar from that zip and read BUILD-FROM-SOURCE.md. The bundle identifies the exact ShinkiEMU and modified Ren'Py fork commits, contains the Rust vendor tree, renpy-build@7bfab40c1174f622f644b24669afd5fb167fbb79 (including FFmpeg/FriBidi sources, patches and link recipes), and readable chardet/certifi source archives with checksums. python-sources.json records versions actually present in the official SDK, rather than substituting the recipe's different versions.

You may modify the LGPL components and rebuild/relink the combined runtime. You may replace compatible runtime libraries and reverse-engineer the work for debugging those modifications. ShinkiEMU imposes no contractual restriction on those rights. Retain ABI-compatible libpython3.12.dll and librenpython.dll exports when replacing the runtime. The source bundle's instructions explain the combined DLL boundary and the separately rebuilt display override.

SDK: Ren'Py 8.5.3.26051504, SHA256 ff57648f9c04f27e381c48af6d8e3ee3cdec296bed4d3831f47f09b0a71b505e. SDK Ren'Py source revision: 39895c1e017f0b36ffea2447d97eccd69d76ee1c. ShinkiEMU modifies the native adapter, host bridge and display override; see the exact fork revision in source-references.json.
