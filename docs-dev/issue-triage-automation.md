# Issue triage automation

OpenQ4 now ships a GitHub Actions workflow that triages **newly opened issues** with conservative, repository-grounded automation.

## Files

- `.github/workflows/issue-triage.yml` — triggers once on `issues.opened`
- `.github/issue-triage.config.json` — label definitions, duplicate thresholds, and response-style settings
- `.github/scripts/issue-triage.mjs` — reads the new issue, repository context, labels, and open issues; calls the configured AI provider; validates the result; applies labels; posts the comment; and closes only high-confidence duplicates
- `.github/scripts/issue-triage.test.mjs` — local test harness for duplicate-scoring and response rendering

## What the workflow does

1. Triggers only when a new issue is opened.
2. Loads repository context from the checked-in docs and roadmap files listed in `.github/issue-triage.config.json`.
3. Fetches the repository's current labels plus all open issues.
4. Heuristically ranks likely duplicate candidates before sending anything to the model.
5. Calls the configured AI provider and asks for **JSON only**.
6. Validates the AI output before taking any write action.
7. Creates configured managed labels only when they are actually needed.
8. Applies labels, posts/updates the maintainer-style triage comment, and closes the issue **only** when both the model and the heuristic duplicate checks strongly agree.
9. If the model call fails, it posts no potentially misleading comment and can fall back to `needs-human-review`.

## Workflow permissions

The workflow uses the minimum permissions needed for triage:

- `contents: read`
- `issues: write`
- `models: read`

`models: read` is only needed for the default GitHub Models provider path.

## Provider and model selection

The workflow defaults to **GitHub Models** and automatically prefers the strongest available GPT-family model when `ISSUE_TRIAGE_MODEL` is not set.

### Repository variables

- `ISSUE_TRIAGE_AI_PROVIDER`
  - Default: `github-models`
  - Supported values: `github-models`, `openai`
- `ISSUE_TRIAGE_MODEL`
  - Optional explicit model override
  - Leave unset to let the script choose the best available GPT-family model for GitHub Models, or default to `gpt-5` for OpenAI
- `ISSUE_TRIAGE_GITHUB_MODELS_ORG`
  - Optional organization attribution for GitHub Models
- `ISSUE_TRIAGE_OPENAI_BASE_URL`
  - Optional custom OpenAI-compatible base URL
- `ISSUE_TRIAGE_DRY_RUN`
  - Optional `true` / `false`
  - When `true`, the workflow logs planned changes but does not write labels, comments, or issue state

### Repository secrets

- `ISSUE_TRIAGE_MODELS_TOKEN`
  - Optional GitHub token with `models:read`
  - Use this if the default `GITHUB_TOKEN` is not sufficient for your GitHub Models setup
- `OPENAI_API_KEY`
  - Required only when `ISSUE_TRIAGE_AI_PROVIDER=openai`

## Managed labels

The automation reuses existing labels first. OpenQ4 currently already uses `bug`, `enhancement`, and `help wanted`, so those stay intact.

Additional labels are defined in `.github/issue-triage.config.json` and are created **only if the workflow actually needs them**, for example:

- `needs-info`
- `needs-human-review`
- `duplicate`
- `question`
- `documentation`
- `security`
- `build/install`
- `compatibility`
- `performance`
- `regression`

If you want fewer or different labels, edit the `managedLabels` and `typeLabelMappings` entries in the config file.

## Tuning duplicate behavior

Duplicate handling is intentionally conservative.

The main knobs live under `duplicateHeuristics` in `.github/issue-triage.config.json`:

- `minimumCandidateScore` — which open issues are even shown to the model
- `humanReviewConfidence` — duplicate confidence that should still stay open but request maintainer review
- `closeConfidence` — duplicate confidence required before closure is allowed
- `minimumFullDuplicateScore` — minimum heuristic similarity score that must agree with the model before closure
- `weights` — relative weighting of title/body/signal/component/phrase overlap

The script will only close an issue as duplicate when:

1. the model says the issue is a **full duplicate**,
2. the model's duplicate confidence is high enough,
3. the matching open issue also has a strong heuristic duplicate score, and
4. the model does not list any remaining new points.

Otherwise the issue stays open and the workflow can add `needs-human-review` instead.

## Response style and maintenance

The response body is generated from structured JSON and always follows the same sections:

- Summary
- Detected points
- Categorisation
- Response / troubleshooting
- Related or duplicate Issues
- Suggested implementation plan
- Status

To adjust tone, point limits, plan length, or which repository docs are included as grounding context, edit `.github/issue-triage.config.json`.

## Local testing

Run the existing repo-side script test plus the new triage test locally:

```bash
python tools/tests/hdr_postprocess_math.py
node --test .github/scripts/issue-triage.test.mjs
```

You can also do a local dry run against a saved `issues.opened` payload:

```bash
ISSUE_TRIAGE_DRY_RUN=true \
GITHUB_EVENT_PATH=/path/to/issues-opened.json \
GITHUB_REPOSITORY=themuffinator/OpenQ4 \
GITHUB_TOKEN=ghp_example \
node .github/scripts/issue-triage.mjs
```

## Notes

- The workflow only reacts to `issues.opened`; comments, edits, relabeling, pull requests, and reopen events do not retrigger it.
- The script treats issue content as untrusted input and explicitly instructs the model to ignore prompt-injection attempts embedded in issue text.
- The automation does **not** remove human-applied labels.
