const {test} = require('node:test');
const assert = require('node:assert/strict');
const {exhausted, reviewed, run} = require('./review-fallback.cjs');
const sha = 'a'.repeat(40);
const refusal = {id: 1, updated_at: '2026-11-04T00:00:00Z', user: {id: 165735046, type: 'Bot'}, performed_via_github_app: {id: 867647}, body: 'Your organization has run out of review credits.\n<!-- greptile-credit-pause:abc-123 -->'};
const success = {...refusal, id: 2, body: `<!-- greptile_summary --> Last reviewed commit: [fix](https://github.com/Reddimus/example/commit/${sha})`};
test('requires exact App identity and credit refusal', () => {
  assert.ok(exhausted(refusal));
  for (const c of [{...refusal,user:{id:2,type:'Bot'}},{...refusal,performed_via_github_app:{id:2}},{...refusal,body:'timeout'},{...refusal,body:'<!-- greptile-credit-pause:abc -->'}]) assert.equal(exhausted(c),false);
  assert.ok(reviewed(success,sha,'Reddimus','example')); assert.equal(reviewed(success,'b'.repeat(40),'Reddimus','example'),false);
});
function fixture(changes={}) {
  const calls=[];
  const pr={state:'open',draft:false,base:{ref:'main'},head:{sha},labels:[],...changes.pr};
  let reads=0;
  const github={paginate:async()=>changes.comments||[refusal],rest:{pulls:{get:async()=>({data:++reads===2 && changes.newHead ? {...pr,head:{sha:changes.newHead}}:pr})},issues:{
    listComments(){},getComment:async()=>({data:changes.refusal||refusal}),
    removeLabel:async()=>calls.push('remove'),addLabels:async()=>calls.push('add'),createComment:async p=>calls.push(p.body)
  }}};
  const context={repo:{owner:'Reddimus',repo:'example'},eventName:'issue_comment',payload:{repository:{default_branch:'main'},sender:{id:165735046},issue:{number:2,pull_request:{}},comment:refusal},...changes.context};
  return {calls,github,context,go:()=>run(github,context,{info(){}})};
}
test('requests native opt-in and records exact head',async()=>{const f=fixture();await f.go();assert.ok(f.calls[0].includes(sha));assert.equal(f.calls[1],'add');});
for (const [name,change] of Object.entries({draft:{pr:{draft:true}},closed:{pr:{state:'closed'}},branch:{pr:{base:{ref:'other'}}},healthy:{comments:[success]},superseded:{newHead:'b'.repeat(40)}})) {
 test(`ignores ${name}`,async()=>{const f=fixture(change);await f.go();assert.deepEqual(f.calls,[]);});
}
test('deduplicates deliveries',async()=>{const f=fixture({comments:[{user:{login:'github-actions[bot]',type:'Bot'},body:`<!-- greptile-fallback:${sha}:1:${refusal.updated_at} -->`}]});await f.go();assert.deepEqual(f.calls,[]);});
test('rearms label only after a fresh credit refusal',async()=>{const f=fixture({pr:{labels:[{name:'review:greptile-quota-fallback'}]}});await f.go();assert.deepEqual(f.calls.slice(1),['remove','add']);});

test('an in-progress summary marker is not a completed review', async()=>{
 const f=fixture({comments:[{...success,updated_at:'2026-11-05T00:00:00Z',body:'<!-- greptile_summary --> Review in progress'}]});
 await f.go();assert.equal(f.calls.at(-1),'add');
});
test('reserves delivery before the external trigger', async()=>{
 const f=fixture(); await f.go(); assert.ok(f.calls[0].startsWith('<!-- greptile-fallback:')); assert.equal(f.calls[1],'add');
});
test('historical retry does not revive an exhausted state after recovery',async()=>{
 const context={repo:{owner:'Reddimus',repo:'example'},eventName:'workflow_dispatch',payload:{inputs:{pr:'2'},repository:{default_branch:'main'}}};
 const f=fixture({comments:[refusal,{...success,id:2,updated_at:'2026-11-05T00:00:00Z',body:success.body.replace(sha,'b'.repeat(40))}],context});
 await f.go();assert.deepEqual(f.calls,[]);
});

const dispatch = {repo:{owner:'Reddimus',repo:'example'},eventName:'workflow_dispatch',payload:{inputs:{pr:'2'},repository:{default_branch:'main'}}};
test('retry succeeds while the latest credit refusal is current',async()=>{const f=fixture({context:dispatch});await f.go();assert.equal(f.calls.at(-1),'add');});
test('retry with no refusal is a no-op',async()=>{const f=fixture({comments:[],context:dispatch});await f.go();assert.deepEqual(f.calls,[]);});
test('a footer cannot borrow a later unrelated commit link',()=>{
 const c={...success,body:`<!-- greptile_summary --> Last reviewed commit: [old](https://github.com/Reddimus/example/commit/${'b'.repeat(40)})\nMention [head](https://github.com/Reddimus/example/commit/${sha})`};
 assert.equal(reviewed(c,sha,'Reddimus','example'),false);
 assert.equal(reviewed(success,sha,'Reddimus','other'),false);
});
test('refusal text quoted in a summary is not a credit event',()=>{assert.equal(exhausted({...refusal,body:refusal.body+'\n<!-- greptile_summary -->'}),false);});
test('non-PR issue comments do nothing',async()=>{const f=fixture();delete f.context.payload.issue.pull_request;await f.go();assert.deepEqual(f.calls,[]);});
test('human markers do not deduplicate deliveries',async()=>{const f=fixture({comments:[refusal,{user:{login:'someone',type:'User'},body:`<!-- greptile-fallback:${sha}:1:date -->`}]});await f.go();assert.equal(f.calls.at(-1),'add');});
test('delivery reservation suppresses automatic redelivery',async()=>{
 const comments=[refusal];const f=fixture({comments});
 f.github.rest.issues.createComment=async p=>{comments.push({user:{login:'github-actions[bot]',type:'Bot'},body:p.body});f.calls.push('reserved');};
 f.github.rest.issues.addLabels=async()=>{f.calls.push('failed');throw new Error('controlled failure');};
 await assert.rejects(f.go(),/controlled failure/); await f.go();assert.deepEqual(f.calls,['reserved','failed']);
});
test('deleted comments are a quiet no-op',async()=>{const f=fixture();f.github.rest.issues.getComment=async()=>{throw Object.assign(new Error('deleted'),{status:404});};await f.go();assert.deepEqual(f.calls,[]);});

test('a completed primary review removes the backup label',async()=>{
 const context={repo:{owner:'Reddimus',repo:'example'},eventName:'issue_comment',payload:{repository:{default_branch:'main'},sender:{id:165735046},issue:{number:2,pull_request:{}},comment:success}};
 const f=fixture({context,refusal:success,pr:{labels:[{name:'review:greptile-quota-fallback'}]}}); await f.go();assert.deepEqual(f.calls,['remove']);
});
test('human edits to an App comment do not enable fallback',async()=>{const f=fixture();f.context.payload.sender={id:1};await f.go();assert.deepEqual(f.calls,[]);});
test('lowercase owner and real sub-footer syntax identify the reviewed commit',()=>{
 const c={...success,body:`<!-- greptile_summary -->\n<sub>Reviews (3) · Last reviewed commit: ["fix"](https://github.com/reddimus/example/commit/${sha}) · more</sub>`};
 assert.ok(reviewed(c,sha,'Reddimus','example'));
});
test('a concurrently removed label does not prevent requesting backup',async()=>{const f=fixture({pr:{labels:[{name:'review:greptile-quota-fallback'}]}});f.github.rest.issues.removeLabel=async()=>{throw Object.assign(new Error('gone'),{status:404});};await f.go();assert.equal(f.calls.at(-1),'add');});

test('a stale webhook reconciles the current refusal body',async()=>{const f=fixture({refusal:{...refusal,updated_at:'2026-11-05T00:00:00Z'}});await f.go();assert.equal(f.calls.at(-1),'add');assert.ok(f.calls[0].includes('2026-11-05'));});
test('a summary event can reconcile a queued refusal event it replaced',async()=>{
 const progress={...success,body:'<!-- greptile_summary --> Review in progress',updated_at:'2026-11-05T00:00:00Z'};
 const f=fixture({comments:[refusal,progress],refusal:progress}); f.context.payload.comment=progress;
 await f.go();assert.equal(f.calls.at(-1),'add');
});
test('a refusal event can reconcile a completed review event it replaced',async()=>{
 const f=fixture({comments:[refusal,success],pr:{labels:[{name:'review:greptile-quota-fallback'}]}});
 await f.go();assert.deepEqual(f.calls,['remove']);
});
test('balanced brackets in commit titles preserve footer recognition',()=>{
 const c={...success,body:`<!-- greptile_summary --> Last reviewed commit: ["fix: handle a[0]"](https://github.com/Reddimus/example/commit/${sha})`};
 assert.ok(reviewed(c,sha,'Reddimus','example'));
});
