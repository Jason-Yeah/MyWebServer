// 无浏览器依赖的表单逻辑测试：模拟 DOM/fetch，不连接服务或数据库。
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert/strict');
const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'resources/index.html'), 'utf8');
assert.match(html, /<link rel="stylesheet" href="\/static\/style\.css">/);
assert.match(html, /<script src="\/static\/app\.js" defer><\/script>/);
assert.match(html, /src="\/static\/pipeline\.png"/);
assert.doesNotMatch(html, /<style>|<script>/);
function form(action) {
    const inputs = {
        username: { value: '', disabled: false, focus() { this.focused = true; } },
        password: { value: '', disabled: false, focus() { this.focused = true; } }
    };
    const button = { textContent: '提交', disabled: false };
    const result = {};
    return {
        inputs, button, result, attributes: {},
        querySelector(s) {
            if (s === 'button') return button;
            if (s === '[data-result]') return result;
            return s.includes('username') ? inputs.username : inputs.password;
        },
        getAttribute() { return action; },
        setAttribute(k, v) { this.attributes[k] = v; },
        removeAttribute(k) { delete this.attributes[k]; },
        addEventListener(_, callback) { this.submit = callback; }
    };
}
const register = form('/register'), login = form('/login');
const calls = [];
let status = 201, fail = false;
const context = {
    TextEncoder, URLSearchParams,
    FormData: class {
        constructor(f) {
            this.values = Object.entries(f.inputs).filter(([, v]) => !v.disabled).map(([k, v]) => [k, v.value]);
        }
        [Symbol.iterator]() { return this.values[Symbol.iterator](); }
    },
    document: {
        querySelectorAll() { return [register, login]; },
        getElementById() { return login.inputs.username; }
    },
    fetch: async (url, options) => {
        calls.push({ url, options });
        if (fail) throw Error('simulated network error');
        return { status, ok: status >= 200 && status < 300, text: async () => '<script>not-executed</script>' };
    }
};
vm.runInNewContext(fs.readFileSync(path.join(root, 'resources/static/app.js'), 'utf8'), context);
async function submit(f, username, password) {
    f.inputs.username.value = username;
    f.inputs.password.value = password;
    await f.submit({ preventDefault() {} });
}
(async () => {
    await submit(register, '测试用户', '中文密码');
    assert.equal(calls.length, 1);
    assert.equal(calls[0].url, '/register');
    assert.equal(calls[0].options.headers['Content-Type'], 'application/x-www-form-urlencoded');
    assert.equal(new URLSearchParams(calls[0].options.body).get('password'), '中文密码');
    assert.equal(register.inputs.password.value, '');
    assert.equal(login.inputs.username.value, '测试用户');
    assert.match(register.result.textContent, /HTTP 201/);
    assert.equal(register.button.disabled, false);
    assert.equal(register.attributes['aria-busy'], undefined);
    status = 401;
    await submit(login, '测试用户', 'wrong-password');
    assert.match(login.result.textContent, /HTTP 401/);
    assert.equal(login.inputs.password.value, 'wrong-password');
    status = 200;
    await submit(login, '测试用户', 'correct-password');
    assert.equal(login.inputs.password.value, '');
    assert.match(login.result.textContent, /未创建登录会话/);
    const count = calls.length;
    await submit(register, 'u'.repeat(65), 'valid-password');
    await submit(register, 'user', '中文'); // 6 字节，不足 8。
    assert.equal(calls.length, count);
    status = 503;
    await submit(register, 'user', 'valid-password');
    assert.match(register.result.textContent, /HTTP 503/);
    fail = true;
    await submit(login, 'user', 'valid-password');
    assert.match(login.result.textContent, /请求未完成/);
    assert.equal(login.button.disabled, false);
    assert.equal(login.inputs.username.disabled, false);
    assert.equal(login.inputs.password.disabled, false);
    console.log('PASS 外部资源引用及模拟 DOM 表单：编码/字节限制/状态提示/清空密码/失败恢复；非真实浏览器视觉验收');
})().catch(error => { console.error(error); process.exitCode = 1; });
