// Only this App's credit refusals can opt a PR into the backup reviewer.
const GREPTILE = 867647;
const BOT = 165735046;
const LABEL = 'review:greptile-quota-fallback';
function trusted(comment) {
  return comment?.user?.id === BOT && comment.user.type === 'Bot'
    && comment.performed_via_github_app?.id === GREPTILE;
}
function exhausted(comment) {
  return trusted(comment)
    && /<!-- greptile-credit-pause:[a-f0-9-]+ -->/.test(comment.body || '')
    && (comment.body || '').includes('Your organization has run out of review credits.');
}
function reviewed(comment, sha) {
  return trusted(comment) && (comment.body || '').includes('<!-- greptile_summary -->')
    && new RegExp('Last reviewed commit:[\\s\\S]*?https://github\\.com/[^/]+/[^/]+/commit/' + sha + '(?:[)\\s"<]|$)').test(comment.body);
}
async function run(github, context, core) {
  const {owner, repo} = context.repo;
  const number = context.eventName === 'workflow_dispatch'
    ? Number(context.payload.inputs.pr) : context.payload.issue?.number;
  if (!Number.isSafeInteger(number) || number < 1) throw new Error('A pull request number is required');
  if (context.eventName !== 'workflow_dispatch' && !context.payload.issue?.pull_request) return;
  const api = github.rest;
  const {data: pr} = await api.pulls.get({owner, repo, pull_number: number});
  if (pr.state !== 'open' || pr.draft || pr.base.ref !== context.payload.repository.default_branch) return;
  const comments = await github.paginate(api.issues.listComments, {owner, repo, issue_number: number, per_page: 100});
  let refusal;
  if (context.eventName === 'workflow_dispatch') {
    refusal = comments.filter(exhausted).sort((a,b) => Date.parse(b.updated_at)-Date.parse(a.updated_at))[0];
  } else {
    // Re-read the comment: edited or deleted webhook payloads are not authority.
    const {data} = await api.issues.getComment({owner, repo, comment_id: context.payload.comment.id});
    refusal = data;
    if (data.updated_at !== context.payload.comment.updated_at) return;
  }
  if (!exhausted(refusal)) return;
  const sha = pr.head.sha;
  if (comments.some(c => reviewed(c, sha))) {
    core.info('Greptile already reviewed the current head');
    return;
  }
  // Do not revive an old refusal after a newer successful primary review.
  if (comments.some(c => trusted(c) && (c.body || '').includes('<!-- greptile_summary -->')
      && Date.parse(c.updated_at) > Date.parse(refusal.updated_at))) return;
  const marker = `<!-- greptile-fallback:${sha}:${refusal.id}:${refusal.updated_at} -->`;
  const own = c => c.user?.login === 'github-actions[bot]' && c.user?.type === 'Bot';
  if (context.eventName !== 'workflow_dispatch' && comments.some(c => own(c) && c.body?.includes(`<!-- greptile-fallback:${sha}:`))) return;
  const {data: current} = await api.pulls.get({owner, repo, pull_number: number});
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) return;
  // Re-adding the label is the native CodeRabbit opt-in event. Push reviews are off.
  if (current.labels.some(l => l.name === LABEL)) {
    await api.issues.removeLabel({owner, repo, issue_number: number, name: LABEL});
  }
  await api.issues.addLabels({owner, repo, issue_number: number, labels: [LABEL]});
  await api.issues.createComment({owner, repo, issue_number: number,
    body: `${marker}\nGreptile refused a review because credits are exhausted. Requested CodeRabbit for ${sha}. This is a request, not proof of a completed review.\n\nRetry after a CodeRabbit rate limit using the Review fallback workflow with PR ${number}.`});
  core.info(`Requested backup review for ${sha}`);
}
module.exports = {exhausted, reviewed, run};
