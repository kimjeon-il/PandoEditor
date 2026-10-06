import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';

const read = file => readFileSync(new URL('../' + file, import.meta.url), 'utf8');

test('boundary timing has a production-linked native target and non-skipping registered contract', () => {
    const cmake = read('app/CMakeLists.txt');
    assert.match(cmake, /add_executable\(m977_boundary_timing_probe\s+\.\.\/tools\/m977-boundary-adoption\/native-probe\.cpp\)/);
    assert.match(cmake, /target_include_directories\(m977_boundary_timing_probe PRIVATE "\$\{PROJECT_SOURCE_DIR\}\/tests"\)/);
    assert.match(cmake, /target_link_libraries\(m977_boundary_timing_probe PRIVATE pandoeditor_editor\)/);
    assert.match(cmake, /qt_add_resources\(m977_boundary_timing_probe sample PREFIX "\/assets"/);
    const contract = cmake.match(/add_test\(NAME m977_boundary_timing_contract[\s\S]*?set_tests_properties\(m977_boundary_timing_contract[\s\S]*?\nendif\(\)/)?.[0];
    assert.ok(contract, 'required boundary timing CTest');
    for (const file of ['runtime', 'suite', 'browser-runner', 'native']) {
        assert.ok(contract.includes(`/tools/m977-boundary-adoption/${file}.test.mjs`), file);
    }
    assert.ok(contract.includes('/tools/m977-boundary-integration.test.mjs'));
    assert.ok(contract.includes('QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;M977_BOUNDARY_PROBE=$<TARGET_FILE:m977_boundary_timing_probe>'));
    assert.ok(contract.includes('WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"'), 'runner refusal test resolves from the source root');
});

test('boundary workflow retains exact-source build, registered test and raw diagnostic evidence in one job', () => {
    const workflow = read('.github/workflows/m977-boundary-timing.yml');
    assert.match(workflow, /contents: read/);
    assert.match(workflow, /persist-credentials: false/);
    assert.match(workflow, /node-version: '24\.19\.0'/);
    assert.match(workflow, /version: '6\.8\.3'/);
    assert.match(workflow, /test "\$\(git rev-parse HEAD\)" = "\$GITHUB_SHA"/);
    assert.match(workflow, /git diff --exit-code HEAD --/);
    assert.match(workflow, /git ls-files -z \| xargs -0 sha256sum > evidence\/source-SHA256SUMS/);
    assert.match(workflow, /cmake --build build --target m977_boundary_timing_probe --parallel 2/);
    assert.match(workflow, /ctest --test-dir build --output-on-failure --no-tests=error/);
    assert.match(workflow, /-R '\^m977_boundary_timing_contract\$'/);
    assert.match(workflow, /--output-junit "\$GITHUB_WORKSPACE\/evidence\/native-ctest\.xml"/);
    assert.match(workflow, /npm ci --prefix tools\/m97\/river\/browser --ignore-scripts/);
    assert.match(workflow, /node --max-old-space-size=512 tools\/m977-boundary-adoption\/browser-runner\.mjs evidence\/chromium/);
    assert.match(workflow, /build\/app\/m977_boundary_timing_probe < evidence\/chromium\/cases\.json > evidence\/native-report\.json/);
    assert.match(workflow, /node tools\/m977-boundary-adoption\/compare-browser\.mjs/);
    assert.match(workflow, /sha256sum -c evidence\/source-SHA256SUMS/);
    assert.match(workflow, /sha256sum -c evidence\/native-SHA256SUMS/);
    for (const file of ['native-configure.log', 'native-build.log', 'native-ctest.log', 'native-build-provenance.json']) {
        assert.ok(workflow.includes(file), file);
    }
    assert.match(workflow, /uses: actions\/upload-artifact@v4\s+if: always\(\)/);
    assert.match(workflow, /name: m977-boundary-timing-\$\{\{ github.sha \}\}/);
    assert.match(workflow, /path: evidence/);
    assert.doesNotMatch(workflow, /continue-on-error:|--target\s+(?:package|portable)|upload-release|git push|git checkout|git reset/);
});
