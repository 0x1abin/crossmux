#!/usr/bin/env python3
"""Fault injection through the CJK pipeline and the real output-set publisher."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
SCRIPTS=ROOT/'lib/EpdFont/scripts'
FONTS=SCRIPTS.parent/'builtinFonts'
spec=importlib.util.spec_from_file_location('publish_cjk',SCRIPTS/'publish-cn-builtin-fonts.py')
publisher=importlib.util.module_from_spec(spec);spec.loader.exec_module(publisher)
NAMES=[f'notosans_cjk_{n}.h' for n in (8,10,12,14,16,18)]+['notosans_cjk_common_intervals.h']
CHARSETS=['cn_common_chars.txt','cn_i18n_chars.txt']

class GenerationTransactionTest(unittest.TestCase):
    def test_generation_or_postprocessing_failure_never_changes_outputs(self):
        for failure in ('fontconvert.py','share-cn-font-intervals.py','compress-cn-builtin-fonts.py','build-font-ids.py'):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);scripts=root/'lib/EpdFont/scripts';fonts=root/'lib/EpdFont/builtinFonts'
                scripts.mkdir(parents=True);fonts.mkdir();(fonts/'source/NotoSansSC').mkdir(parents=True)
                (root/'src').mkdir();font_ids=root/'src/fontIds.h';font_ids.write_text('old font IDs')
                (fonts/'source/NotoSansSC/NotoSansSC-Regular.otf').write_bytes(b'fixture')
                for name in CHARSETS:shutil.copy2(SCRIPTS/name,scripts/name)
                for name in NAMES:shutil.copy2(FONTS/name,fonts/name)
                for name in ['build-cn-builtin-fonts.sh','share-cn-font-intervals.py','compress-cn-builtin-fonts.py','publish-cn-builtin-fonts.py','verify_compression.py','build-font-ids.py']:
                    shutil.copy2(SCRIPTS/name,scripts/name)
                wrapper=root/'python';wrapper.write_text(f'#!{sys.executable}\n'+r'''
import os,sys,re,subprocess
from pathlib import Path
args=sys.argv[1:];step=Path(args[0]).name
if step==os.environ['FAIL_STAGE']:
 if step!='fontconvert.py' or args[2]=='12':sys.exit(7)
if args[0]=='-m':
 for arg in args:
  if arg.startswith('--output-file='):Path(arg.split('=',1)[1]).write_bytes(b'fixture')
 sys.exit(0)
if step=='fontconvert.py':
 fonts=Path(os.environ['REAL_FONTS']);name=args[1];text=(fonts/(name+'.h')).read_text()
 if args[2] in ('8','10','12'):
  shared=(fonts/'notosans_cjk_common_intervals.h').read_text()
  body=re.search(r'notosans_cjk_commonIntervals\[\] = \{(.*?)\n\};',shared,re.S).group(1)
  text=text.replace('#include "notosans_cjk_common_intervals.h"\n','')
  text=text.replace('notosans_cjk_commonIntervals,',name+'Intervals,')
  text=text.replace('static const EpdFontData '+name+' = {','static const EpdUnicodeInterval '+name+'Intervals[] = {'+body+'\n};\nstatic const EpdFontData '+name+' = {')
 print(text,end='');sys.exit(0)
subprocess.run([sys.executable,*args],check=True)
''');wrapper.chmod(0o755)
                before={p:p.read_bytes() for p in [*(fonts/n for n in NAMES),*(scripts/n for n in CHARSETS),font_ids]}
                result=subprocess.run(['bash',str(scripts/'build-cn-builtin-fonts.sh')],env=dict(os.environ,SKIP_CHARSET='1',PYTHON=str(wrapper),REAL_FONTS=str(FONTS),FAIL_STAGE=failure),capture_output=True)
                self.assertNotEqual(result.returncode,0,result.stdout.decode())
                self.assertEqual(before,{p:p.read_bytes() for p in before})
                self.assertFalse(list(fonts.glob('.cjk-stage.*')))

    def test_publication_failure_restores_every_previous_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);stage=root/'stage';dest=root/'builtinFonts';scripts=root/'scripts'
            stage.mkdir();dest.mkdir();scripts.mkdir()
            for name in NAMES+CHARSETS:
                (stage/name).write_text('new '+name)
                ((scripts if name in CHARSETS else dest)/name).write_text('old '+name)
            before={p:p.read_bytes() for p in [*dest.iterdir(),*scripts.iterdir()]}
            replace=Path.replace;calls=0
            def fail_once(path,target):
                nonlocal calls
                calls+=1
                if calls==4:raise OSError('injected publish failure')
                return replace(path,target)
            with patch.object(Path,'replace',fail_once):
                with self.assertRaises(OSError):publisher.publish(stage,dest)
            self.assertEqual(before,{p:p.read_bytes() for p in before})
            self.assertFalse(list(root.glob('.cjk-backup-*')))

    def test_font_id_publication_failure_restores_all_fonts(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);stage=root/'stage';dest=root/'builtinFonts';scripts=root/'scripts'
            stage.mkdir();dest.mkdir();scripts.mkdir();(root/'src').mkdir()
            ids=root/'src/fontIds.h';ids.write_text('old IDs');(stage/'fontIds.h').write_text('new IDs')
            for name in NAMES+CHARSETS:
                (stage/name).write_text('new '+name)
                ((scripts if name in CHARSETS else dest)/name).write_text('old '+name)
            before={p:p.read_bytes() for p in [*dest.iterdir(),*scripts.iterdir(),ids]}
            replace=Path.replace
            def fail_ids(path,target):
                if target==ids:raise OSError('injected font ID publication failure')
                return replace(path,target)
            with patch.object(Path,'replace',fail_ids):
                with self.assertRaises(OSError):publisher.publish(stage,dest,ids)
            self.assertEqual(before,{p:p.read_bytes() for p in before})
            for name in NAMES+CHARSETS:(stage/name).write_text('new '+name)
            publisher.publish(stage,dest,ids)
            self.assertEqual(ids.read_text(),'new IDs')
            for name in NAMES+CHARSETS:
                self.assertEqual(((scripts if name in CHARSETS else dest)/name).read_text(),'new '+name)
            self.assertFalse(list(root.glob('.cjk-backup-*')))

    def test_missing_font_ids_rejects_set_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);stage=root/'stage';dest=root/'builtinFonts'
            stage.mkdir();dest.mkdir()
            for name in NAMES+CHARSETS:(stage/name).write_text('new '+name)
            with self.assertRaises(ValueError):publisher.publish(stage,dest,root/'fontIds.h')
            self.assertFalse(list(dest.iterdir()))

if __name__=='__main__':unittest.main()
