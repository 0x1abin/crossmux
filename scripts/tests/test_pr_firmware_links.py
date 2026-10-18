"""Execute the firmware-comment workflow script against GitHub API fixtures."""
import json
from pathlib import Path
import shutil
import subprocess
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = textwrap.dedent((ROOT / '.github/workflows/pr-firmware-links.yml').read_text().split('script: |\n', 1)[1])
NAMES = ['firmware.bin', 'firmware-sticky.bin', 'firmware-x4pro.bin', 'firmware-x4c.bin', 'firmware-papermono.bin']
RUNNER = r'''
const fixture = JSON.parse(require('fs').readFileSync(0, 'utf8'));
const writes = [], notices = [];
const run = {id:123,head_sha:'a'.repeat(40),head_branch:'topic',
  head_repository:{owner:{login:'contributor'}},
  html_url:'https://github.com/0x1abin/crossmux/actions/runs/123',
  conclusion:fixture.conclusion || 'success'};
const pr = {number:7,state:'open',head:{sha:run.head_sha}};
const github = {rest:{
  pulls:{list:async()=>({data:[pr]}),get:async()=>({data:{...pr,
    head:{sha:fixture.superseded ? 'b'.repeat(40) : run.head_sha}}})},
  actions:{listWorkflowRunArtifacts:async()=>({data:{artifacts:fixture.artifacts || []}})},
  issues:{listComments:()=>{},
    updateComment:async value=>writes.push({kind:'update',...value}),
    createComment:async value=>writes.push({kind:'create',...value})}},
  paginate:async()=>fixture.comments || []};
const context = {repo:{owner:'0x1abin',repo:'crossmux'},payload:{workflow_run:run}};
const core = {notice:value=>notices.push(value)};
const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;
new AsyncFunction('github','context','core',fixture.script)(github,context,core)
  .then(()=>process.stdout.write(JSON.stringify({writes,notices})))
  .catch(error=>{console.error(error);process.exitCode=1;});
'''


@unittest.skipUnless(shutil.which('node'), 'node is required to execute the workflow JavaScript')
class PrFirmwareLinksTest(unittest.TestCase):
    def run_workflow(self, **fixture):
        result = subprocess.run(['node', '-e', RUNNER], input=json.dumps({'script': SCRIPT, **fixture}),
                                text=True, capture_output=True, check=True)
        return json.loads(result.stdout)['writes']

    def test_partial_build_keeps_the_available_link(self):
        writes = self.run_workflow(artifacts=[{'id': 1, 'name': NAMES[0], 'expired': False},
                                             {'id': 2, 'name': NAMES[2], 'expired': True},
                                             {'id': 3, 'name': 'unrelated', 'expired': False}])
        body = writes[0]['body']
        self.assertIn('[X3 / X4 (`firmware.bin`)]', body)
        self.assertIn('/actions/runs/123/artifacts/1', body)
        self.assertNotIn('/artifacts/2', body)
        self.assertNotIn('/artifacts/3', body)
        self.assertNotIn('Firmware is not available', body)
        self.assertNotIn('X4 Pro', body)

    def test_all_supported_builds_remain_linked(self):
        artifacts = [{'id': i + 1, 'name': name, 'expired': False} for i, name in enumerate(NAMES)]
        body = self.run_workflow(artifacts=artifacts)[0]['body']
        for artifact in artifacts:
            self.assertIn(f"/artifacts/{artifact['id']}", body)

    def test_missing_or_expired_artifacts_show_no_download(self):
        for artifacts in ([], [{'id': 1, 'name': NAMES[0], 'expired': True}]):
            with self.subTest(artifacts=artifacts):
                body = self.run_workflow(artifacts=artifacts)[0]['body']
                self.assertIn('Firmware is not available', body)
                self.assertNotIn('/artifacts/', body)

    def test_other_ci_failures_do_not_hide_a_successful_build(self):
        body = self.run_workflow(conclusion='failure', artifacts=[{'id': 1, 'name': NAMES[0]}])[0]['body']
        self.assertIn('/artifacts/1', body)
        self.assertIn('another CI check finished with failure', body)

    def test_superseded_run_does_not_write_a_comment(self):
        self.assertEqual(self.run_workflow(superseded=True), [])

    def test_existing_bot_comment_is_updated(self):
        comment = {'id': 42, 'user': {'login': 'github-actions[bot]'}, 'body': '<!-- pr-firmware-artifacts -->'}
        writes = self.run_workflow(comments=[comment])
        self.assertEqual((writes[0]['kind'], writes[0]['comment_id']), ('update', 42))

    def test_human_comment_is_not_overwritten(self):
        comment = {'id': 42, 'user': {'login': 'contributor'}, 'body': '<!-- pr-firmware-artifacts -->'}
        writes = self.run_workflow(comments=[comment])
        self.assertEqual(writes[0]['kind'], 'create')


if __name__ == '__main__':
    unittest.main()
