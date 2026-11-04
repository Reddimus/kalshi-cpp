const {test} = require('node:test');
const assert = require('node:assert/strict');
const {exhausted, reviewed, run} = require('./review-fallback.cjs');
const sha = 'a'.repeat(40);
const refusal = {id: 1, updated_at: '2026-11-04T00:00:00Z', user: {id: 165735046, type: 'Bot'}, performed_via_github_app: {id: 867647}, body: 'Your organization has run out of review credits.\n<!-- greptile-credit-pause:abc-123 -->'};
const success = {...refusal, body: `<!-- greptile_summary --> Last reviewed commit: [fix](https://github.com/Reddimus/example/commit/${sha})`};
test('requires exact App identity and credit refusal', () => {
  assert.ok(exhausted(refusal));
  for (const c of [{...refusal,user:{id:2,type:'Bot'}},{...refusal,performed_via_github_app:{id:2}},{...refusal,body:'timeout'},{...refusal,body:'<!-- greptile-credit-pause:abc -->'}]) assert.equal(exhausted(c),false);
  assert.ok(reviewed(success,sha)); assert.equal(reviewed(success,'b'.repeat(40)),false);
});
function fixture(changes={}) {
  const calls=[];
  const pr={state:'open',draft:false,base:{ref:'main'},head:{sha},labels:[],...changes.pr};
  let reads=0;
  const github={paginate:async()=>changes.comments||[],rest:{pulls:{get:async()=>({data:++reads===2 && changes.newHead ? {...pr,head:{sha:changes.newHead}}:pr})},issues:{
    listComments(){},getComment:async()=>({data:changes.refusal||refusal}),
    removeLabel:async()=>calls.push('remove'),addLabels:async()=>calls.push('add'),createComment:async p=>calls.push(p.body)
  }}};
  const context={repo:{owner:'Reddimus',repo:'example'},eventName:'issue_comment',payload:{repository:{default_branch:'main'},issue:{number:2,pull_request:{}},comment:refusal},...changes.context};
  return {calls,go:()=>run(github,context,{info(){}})};
}
test('requests native opt-in and records exact head',async()=>{const f=fixture();await f.go();assert.equal(f.calls[0],'add');assert.ok(f.calls[1].includes(sha));});
for (const [name,change] of Object.entries({draft:{pr:{draft:true}},closed:{pr:{state:'closed'}},branch:{pr:{base:{ref:'other'}}},healthy:{comments:[success]},stale:{refusal:{...refusal,updated_at:'2026-11-05T00:00:00Z'}},superseded:{newHead:'b'.repeat(40)},recovered:{comments:[{...success,updated_at:'2026-11-05T00:00:00Z',body:'<!-- greptile_summary -->'}]}})) {
 test(`ignores ${name}`,async()=>{const f=fixture(change);await f.go();assert.deepEqual(f.calls,[]);});
}
test('deduplicates deliveries',async()=>{const f=fixture({comments:[{user:{login:'github-actions[bot]',type:'Bot'},body:`<!-- greptile-fallback:${sha}:1:${refusal.updated_at} -->`}]});await f.go();assert.deepEqual(f.calls,[]);});
test('rearms label only after a fresh credit refusal',async()=>{const f=fixture({pr:{labels:[{name:'review:greptile-quota-fallback'}]}});await f.go();assert.deepEqual(f.calls.slice(0,2),['remove','add']);});
