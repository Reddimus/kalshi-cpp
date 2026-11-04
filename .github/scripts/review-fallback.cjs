// Only this App's credit refusals can opt a PR into the backup reviewer.
const GREPTILE = 867647;
const BOT = 165735046;
const LABEL = 'review:greptile-quota-fallback';
function trusted(comment) {
  return comment?.user?.id === BOT && comment.user.type === 'Bot'
    && comment.performed_via_github_app?.id === GREPTILE;
}
function exhausted(comment) {
  const body = comment?.body || '';
  return trusted(comment) && /<!-- greptile-credit-pause:[a-f0-9-]+ -->/.test(body)
    && body.includes('Your organization has run out of review credits.')
    && !body.includes('<!-- greptile_summary -->');
}
function reviewedCommit(comment, owner, repo) {
  const body = comment?.body || '';
  if (!trusted(comment) || !body.includes('<!-- greptile_summary -->')) return;
  const label = /Last reviewed commit:\s*/.exec(body);
  if (!label) return;
  let cursor = label.index + label[0].length;
  if (body[cursor] !== '[') return;
  // Markdown link captions can contain balanced brackets, e.g. `fix a[0]`.
  let depth = 0;
  for (; cursor < body.length; cursor++) {
    const char = body[cursor];
    if (char === '\n') return;
    if (char === '\\') { cursor++; continue; }
    if (char === '[') depth++;
    if (char === ']' && --depth === 0) break;
  }
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const target = new RegExp(String.raw`^\(https://github\.com/${escape(owner)}/${escape(repo)}/commit/([a-f0-9]{40})\)`, 'i');
  return body.slice(cursor + 1).match(target)?.[1].toLowerCase();
}
function reviewed(comment, sha, owner, repo) {
  return reviewedCommit(comment, owner, repo) === sha;
}
const newest = (a, b) => Date.parse(b.updated_at) - Date.parse(a.updated_at) || b.id - a.id;
async function run(github, context, core) {
  const {owner, repo} = context.repo;
  const manual = context.eventName === 'workflow_dispatch';
  if (!manual && (!context.payload.issue?.pull_request || context.payload.sender?.id !== BOT)) return;
  const number = manual ? Number(context.payload.inputs.pr) : context.payload.issue.number;
  if (!Number.isSafeInteger(number) || number < 1) throw new Error('A pull request number is required');
  const api = github.rest;
  const args = {owner, repo, issue_number: number};
  async function removeOptIn() {
    try { await api.issues.removeLabel({...args, name: LABEL}); }
    catch (error) { if (error.status !== 404) throw error; }
  }
  const {data: pr} = await api.pulls.get({owner, repo, pull_number: number});
  if (pr.state !== 'open' || pr.draft || pr.base.ref !== context.payload.repository.default_branch) {
    core.info('Backup reviews require an open, ready PR targeting the default branch');
    return;
  }
  let comments = await github.paginate(api.issues.listComments, {...args, per_page: 100});
  if (!manual) {
    // Refresh the triggering record, but reconcile the whole PR. GitHub may
    // replace a pending event with a newer event in this concurrency group.
    const id = context.payload.comment.id;
    comments = comments.filter(c => c.id !== id);
    try {
      const {data} = await api.issues.getComment({owner, repo, comment_id: id});
      comments.push(data);
    } catch (error) { if (error.status !== 404) throw error; }
  }
  const sha = pr.head.sha;
  if (comments.some(c => reviewed(c, sha, owner, repo))) {
    if (pr.labels.some(l => l.name === LABEL)) await removeOptIn();
    core.info('Primary review completed; backup opt-in removed');
    return;
  }
  // Refusals report credit state without a SHA. A newer completed primary
  // review ends that evidence; in-progress summaries are not completion.
  const latest = comments.filter(c => exhausted(c) || reviewedCommit(c, owner, repo)).sort(newest)[0];
  if (!exhausted(latest)) { core.info('No current authenticated credit refusal'); return; }
  const marker = `<!-- greptile-fallback:${sha}:${latest.id}:${latest.updated_at} -->`;
  const own = c => c.user?.login === 'github-actions[bot]' && c.user?.type === 'Bot';
  if (!manual && comments.some(c => own(c) && c.body?.includes(`<!-- greptile-fallback:${sha}:`))) return;
  const {data: current} = await api.pulls.get({owner, repo, pull_number: number});
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) return;
  // Reserve before triggering the external App. Failed delivery needs an
  // explicit retry, rather than an automatic request that may duplicate it.
  await api.issues.createComment({...args,
    body: `${marker}\nGreptile reported exhausted review credits. Requesting CodeRabbit; observed PR head: ${sha}. The App may see a newer head. Verify its reviewed commit before merging.\n\nIf delivery fails or CodeRabbit is rate-limited, retry Review fallback from Actions with PR ${number} after checking the current feedback.`});
  if (current.labels.some(l => l.name === LABEL)) await removeOptIn();
  await api.issues.addLabels({...args, labels: [LABEL]});
  core.info(`Requested backup review for observed head ${sha}`);
}
module.exports = {exhausted, reviewed, run};
