"""Check sync-build event selection without dispatching a GitHub workflow."""

import fnmatch
import re
import unittest
from pathlib import Path
from types import SimpleNamespace


class SyncBuildWorkflowTest(unittest.TestCase):
    def test_event_selection(self):
        workflow = Path(__file__).resolve().parents[2] / '.github/workflows/sync-build.yml'
        source = workflow.read_text()
        condition = re.search(r'    if: \|\n((?:      [^\n]*\n)+)', source)
        self.assertIsNotNone(condition)
        expression = ' '.join(condition[1].split()).replace('||', 'or').replace('&&', 'and')
        push = re.search(r'  push:\n    branches: \[([^\]]+)\]', source)
        self.assertIsNotNone(push)
        branches = [item.strip().strip("'\"") for item in push[1].split(',')]
        self.assertIn('  workflow_dispatch:', source)
        cases = [
            ('workflow_dispatch', 'main', True),
            ('push', 'codex/sync-upstream-abc12345', True),
            ('pull_request', 'codex/sync-upstream-abc12345', True),
            ('push', 'codex/ordinary-change', False),
            ('pull_request', 'codex/ordinary-change', False),
            ('push', 'chore/sync-upstream', False),
            ('pull_request', 'chore/sync-upstream', False),
            ('push', 'codex/sync-upstream', False),
            ('pull_request', 'codex/sync-upstream-other/review', True),
        ]
        for event, branch, expected in cases:
            with self.subTest(event=event, branch=branch):
                github = SimpleNamespace(
                    event_name=event,
                    ref='refs/heads/' + branch if event != 'pull_request' else 'refs/pull/1/merge',
                    head_ref=branch if event == 'pull_request' else '',
                )
                # Evaluate only this repository-authored condition with its two used primitives.
                selected = eval(expression, {'__builtins__': {}}, {'github': github, 'startsWith': str.startswith})
                self.assertEqual(selected, expected)
                if event == 'push':
                    self.assertEqual(any(fnmatch.fnmatchcase(branch, pattern) for pattern in branches), expected)


if __name__ == '__main__':
    unittest.main()
