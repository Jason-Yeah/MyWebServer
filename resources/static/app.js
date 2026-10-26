// 只增强表单体验；关闭脚本时仍可使用原生 POST 表单。
const messages = {
    200: '密码校验通过。当前仅验证凭据，未创建登录会话。',
    201: '注册成功。可以使用验证表单检查这个账号的密码。',
    400: '输入格式不符合要求，请检查用户名和密码。',
    401: '用户名或密码不正确。',
    409: '用户名已被注册，请更换用户名或尝试验证密码。',
    415: '服务器不接受当前表单格式。',
    503: '认证服务不可用，请检查 MYWEB_AUTH 开关、数据库连接及权限。'
};
const bytes = new TextEncoder();
document.querySelectorAll('form').forEach(form => {
    const button = form.querySelector('button');
    const result = form.querySelector('[data-result]');
    form.addEventListener('submit', async event => {
        event.preventDefault();
        if (button.disabled) return;
        const username = form.querySelector('[name="username"]');
        const password = form.querySelector('[name="password"]');
        const usernameSize = bytes.encode(username.value).length;
        const passwordSize = bytes.encode(password.value).length;
        if (usernameSize < 1 || usernameSize > 64 || passwordSize < 8 || passwordSize > 128) {
            result.className = 'result error';
            result.textContent = '请检查字节长度：用户名 1–64 字节，密码 8–128 字节。';
            (usernameSize < 1 || usernameSize > 64 ? username : password).focus();
            return;
        }
        const body = new URLSearchParams(new FormData(form));
        const label = button.textContent;
        username.disabled = true;
        password.disabled = true;
        button.disabled = true;
        button.textContent = '正在处理…';
        form.setAttribute('aria-busy', 'true');
        result.className = 'result';
        result.textContent = '请求处理中，请稍候…';
        try {
            const response = await fetch(form.getAttribute('action'), {
                method: 'POST',
                // 后端精确匹配此类型，因此不附加 charset 参数。
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                credentials: 'omit',
                cache: 'no-store',
                body: body.toString()
            });
            // 完整读取响应；使用固定提示，不把服务器内容作为 HTML 执行。
            await response.text();
            result.className = response.ok ? 'result success' : 'result error';
            result.textContent = `HTTP ${response.status} · ${messages[response.status] || '收到未预期的响应，请查看服务日志。'}`;
            if (response.ok) {
                password.value = '';
                const loginName = document.getElementById('login-username');
                if (response.status === 201 && !loginName.value) loginName.value = username.value;
            }
        } catch (error) {
            result.className = 'result error';
            result.textContent = '请求未完成，请确认服务仍在运行。若刚刚提交注册，可先尝试验证，避免重复提交。';
        } finally {
            username.disabled = false;
            password.disabled = false;
            button.disabled = false;
            button.textContent = label;
            form.removeAttribute('aria-busy');
        }
    });
});
