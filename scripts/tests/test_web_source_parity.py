"""Run the production bilingual web scripts and build their actual gzip assets."""
import gzip
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / "src/network/html"


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run_node(program):
    subprocess.run(["node"], input=program, text=True, check=True)


class WebSourceParityTest(unittest.TestCase):
    def test_opds_header_slots_preserve_change_clear_and_cancel(self):
        for locale in ["en", "zh-CN"]:
            with self.subTest(locale=locale):
                source = (HTML / locale / "SettingsPage.html").read_text()
                functions = "\n".join(function(source, name) for name in [
                    "function escapeHtml(", "function renderOpdsHeaderRows(",
                    "async function saveOpdsServer(", "async function deleteOpdsServer("])
                run_node(r'''
const assert = require('node:assert/strict');
const fields = Object.create(null);
const document = {getElementById: id => fields[id] || (fields[id] = {value: ''})};
const OPDS_MAX_CUSTOM_HEADERS = 2;
let requests = [], confirmResult = false;
const fetch = async (url, options) => { requests.push({url, body: JSON.parse(options.body)}); return {ok: true}; };
const showWebToast = () => {};
const showWebConfirm = async () => confirmResult;
const loadOpdsServers = async () => {};
''' + functions + r'''
(async () => {
  const markup = renderOpdsHeaderRows({headers:[{name:'X-Access',hasValue:true},{name:'X-Other',hasValue:false}]}, 2);
  assert(markup.includes('type="password"'));
  assert(markup.includes('opds-header0-name-2') && markup.includes('opds-header1-value-2'));
  assert(!markup.includes('secret'));
  const set = (id, value) => document.getElementById(id).value = value;
  set('opds-name-2','Library'); set('opds-url-2','https://example.test'); set('opds-user-2','reader');
  set('opds-header0-name-2',' X-Access '); set('opds-header0-value-2','');
  set('opds-header1-name-2',''); set('opds-header1-value-2','');
  await saveOpdsServer(2);
  assert.deepEqual(requests[0].body.headers,[{name:'X-Access'},{name:'',value:''}]);
  assert.equal(requests[0].body.index,2); assert(!('password' in requests[0].body));
  set('opds-header0-value-2','new-secret'); set('opds-header1-name-2','X-Other');
  await saveOpdsServer(2);
  assert.deepEqual(requests[1].body.headers,[{name:'X-Access',value:'new-secret'},{name:'X-Other'}]);
  requests=[]; await deleteOpdsServer(2); assert.equal(requests.length,0);
  confirmResult=true; await deleteOpdsServer(2); assert.equal(requests.length,1);
  assert.equal(requests[0].url,'/api/opds/delete');
})().catch(error => {console.error(error); process.exitCode=1;});
''')
                if locale == "zh-CN":
                    self.assertIn(">请求头 ", source)
                    self.assertIn(" 名称</span>", source)
                    self.assertIn(" 值</span>", source)
                    self.assertIn("（保持不变）", source)

    def test_localized_dialog_queue_cancel_confirm_and_toast(self):
        for locale, suffix, title, confirm, notice in [
            ("en", "", "Confirm action", "Confirm", "Notice"),
            ("zh-CN", ".zh-CN", "确认操作", "确认", "提示")
        ]:
            with self.subTest(locale=locale):
                footer = (HTML / "shared" / ("Footer"+suffix+".html.inc")).read_text()
                script = re.search(r"<script>(.*?)</script>", footer, re.S).group(1)
                run_node(r'''
const assert = require('node:assert/strict');
const elements = Object.create(null);
const document = {getElementById: id => elements[id] || (elements[id]={
  textContent:'',style:{}, listeners:[], returnValue:'', opens:0,
  addEventListener(type, fn) {this.listeners.push(fn);},
  showModal() {this.opens++;},
  close(value) {this.returnValue=value;this.listeners.shift()();}
})};
let timers=0, cleared=[];
const setTimeout=()=>++timers, clearTimeout=id=>cleared.push(id);
''' + script + "\nconst expected="+repr([title, confirm, notice]).replace("'", '"')+";\n"+r'''
(async () => {
  const first=showWebConfirm('book?'), second=showWebConfirm('next?');
  await Promise.resolve(); await Promise.resolve();
  assert.equal(webMessageDialog.opens,1);
  assert.equal(webDialogTitle.textContent,expected[0]);
  assert.equal(webDialogConfirm.textContent,expected[1]);
  assert.equal(webDialogCancel.hidden,false);
  webMessageDialog.close(''); assert.equal(await first,false);
  await Promise.resolve(); await Promise.resolve();
  assert.equal(webMessageDialog.opens,2);
  webMessageDialog.close('confirm'); assert.equal(await second,true);
  const alert=showWebAlert('message');
  await Promise.resolve(); await Promise.resolve();
  assert.equal(webDialogTitle.textContent,expected[2]);assert.equal(webDialogCancel.hidden,true);
  webMessageDialog.close('confirm');await alert;
  showWebToast('one',false); showWebToast('two',true);
  assert.equal(webToast.textContent,'two');assert.equal(webToast.className,'toast error');
  assert.equal(cleared.at(-1),1);
})().catch(error=>{console.error(error);process.exitCode=1;});
''')

    def test_file_dialog_cancellation_cleanup_and_nfc_names(self):
        for locale in ["en", "zh-CN"]:
            with self.subTest(locale=locale):
                source = (HTML / locale / "FilesPage.html").read_text()
                for name in ["uploadModal","folderModal","deleteModal","renameModal","moveModal","imagePreviewModal"]:
                    self.assertIn('<dialog class="modal-overlay" id="'+name+'"',source)
                    self.assertIn('id="'+name+'Title"',source)
                self.assertNotRegex(source, r"\balert\(")
                self.assertIn("modal.addEventListener('cancel'",source)
                self.assertIn("event.preventDefault()",source)
                functions="\n".join(function(source, name) for name in [
                    "function openWebModal(", "function closeWebModal(", "function closeUploadModal(",
                    "function handleCancelUploadModal(", "function reserveAvailableUploadFilename("])
                run_node(r'''
const assert=require('node:assert/strict');
const elements=Object.create(null);
const document={getElementById:id=>elements[id]||(elements[id]={
  open:false,style:{},value:'pending',classList:{remove(){}},
  showModal(){this.open=true;},close(){this.open=false;}
}),querySelector:()=>({classList:{remove(){}}})};
let isUploadInProgress=false,operationCancelled=false,currentUploadWs=null,currentUploadXhr=null;
let epubImagesCache=['old'], imageStates={old:true},autoCropProtectedPaths=new Set(['old']),logSection=null;
const applyUploadSettings=()=>{};
''' + functions + r'''
const modal=document.getElementById('uploadModal');
openWebModal('uploadModal');assert(modal.open);
let closed=0,aborted=0;
isUploadInProgress=true;
currentUploadWs={close(){closed++;}};currentUploadXhr={abort(){aborted++;}};
closeUploadModal();assert(modal.open);assert.equal(closed,0);
handleCancelUploadModal();assert(modal.open);assert(operationCancelled);
assert.equal(closed,1);assert.equal(aborted,1);
isUploadInProgress=false;handleCancelUploadModal();assert(!modal.open);
assert.equal(document.getElementById('fileInput').value,'');assert.deepEqual(epubImagesCache,[]);
const names=new Set(['café.epub']);
assert.equal(reserveAvailableUploadFilename('cafe\u0301.epub',names),'café (2).epub');
assert.equal(reserveAvailableUploadFilename('another.epub',names),'another.epub');
''')

    def test_build_assets_include_localized_helpers_and_valid_scripts(self):
        with tempfile.TemporaryDirectory(prefix="web-source-parity-") as directory:
            temp=Path(directory)
            target=temp/"src/network/html"
            shutil.copytree(HTML,target,ignore=shutil.ignore_patterns("*.generated.h"))
            result=subprocess.run(["python3",str(ROOT/"scripts/build_html.py")],cwd=temp,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for locale in ["en","zh-CN"]:
                for name in ["FilesPage","FontsPage","HomePage","SettingsPage"]:
                    header=(target/locale/(name+"Html.generated.h")).read_text()
                    array=header[header.index("{")+1:header.index("};")]
                    raw=gzip.decompress(bytes(int(token,16) for token in re.findall(r"0x([0-9a-f]{2})",array))).decode()
                    self.assertNotIn("<!--#include",raw)
                    self.assertIn("function showWebConfirm(",raw)
                    if locale=="zh-CN":
                        self.assertIn("确认操作",raw)
                        self.assertNotIn("'Confirm action'",raw)
                    scripts=re.findall(r"<script(?:\s[^>]*)?>(.*?)</script>",raw,re.S)
                    for script in scripts:
                        checked=subprocess.run(["node","--check"],input=script,text=True,capture_output=True)
                        self.assertEqual(checked.returncode,0,checked.stderr)


if __name__=="__main__":
    unittest.main()
