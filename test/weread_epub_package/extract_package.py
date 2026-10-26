"""Compile the production package functions with the host storage implementation."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
functions = []
for signature in (
    'bool chapterHasNotes(', 'Error writePackageFiles(', 'Error packageBook('
):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    functions.append(source[begin:end])
Path(sys.argv[2]).write_text('\n\n'.join(functions))
