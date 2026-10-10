import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

const $ = (id) => document.getElementById(id);
const viewport = $('viewport');
const canvas = $('model-canvas');
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
const cameraButtons = [...document.querySelectorAll('[data-camera]')];
let renderer;

function showError(message) {
  viewport.setAttribute('aria-busy', 'false');
  $('load-state').hidden = false;
  $('load-state').classList.add('error');
  $('load-title').textContent = 'Unable to open the review asset';
  $('load-detail').textContent = message;
  $('scene-state').textContent = 'REVIEW UNAVAILABLE';
  $('retry').hidden = false;
  $('motion-controls').disabled = true;
  $('surface-controls').disabled = true;
  cameraButtons.forEach((button) => { button.disabled = true; });
  $('reset-camera').disabled = true;
}
$('retry').addEventListener('click', () => window.location.reload());

try {
  renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
} catch (error) {
  showError('WebGL 2 could not start. Enable hardware acceleration or open this page in a current desktop or mobile browser.');
  console.error(error);
}

if (renderer) {
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setClearColor(0x000000, 0);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.2;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(36, 1, 0.05, 500);
  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = !reducedMotion.matches;
  controls.dampingFactor = 0.09;
  controls.maxPolarAngle = Math.PI / 2;
  controls.screenSpacePanning = true;
  controls.enabled = false;

  const pmrem = new THREE.PMREMGenerator(renderer);
  const room = new RoomEnvironment();
  const environment = pmrem.fromScene(room, 0.04);
  scene.environment = environment.texture;
  scene.environmentIntensity = 0.65;
  room.dispose();
  pmrem.dispose();

  const hemisphere = new THREE.HemisphereLight(0xc8dfe9, 0x394044, 1.8);
  const key = new THREE.DirectionalLight(0xffe6c8, 3.7);
  key.position.set(6, 10, 6);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  key.shadow.normalBias = 0.025;
  key.shadow.bias = -0.0001;
  const fill = new THREE.DirectionalLight(0xc7e4ff, 1.4);
  fill.position.set(-5, 4, 6);
  const rim = new THREE.DirectionalLight(0xc9e9ff, 3);
  rim.position.set(-3, 7, -6);
  scene.add(hemisphere, key, key.target, fill, rim);

  const floor = new THREE.Mesh(new THREE.PlaneGeometry(200, 200), new THREE.ShadowMaterial({ color: 0x070d10, opacity: 0.38 }));
  floor.rotation.x = -Math.PI / 2;
  floor.receiveShadow = true;
  scene.add(floor);
  const grid = new THREE.GridHelper(30, 30, 0x657d84, 0x435960);
  grid.material.transparent = true;
  grid.material.opacity = 0.22;
  grid.material.depthWrite = false;
  scene.add(grid);

  let model;
  let mixer;
  let action;
  let clips = [];
  let materials = [];
  let teamMaterials = [];
  let clipSpecs = [];
  let catalogue = [];
  let selection = 0;
  let manifestRequest;
  const download = document.querySelector('.download-link');
  download.addEventListener('click', (event) => {
    if (download.getAttribute('aria-disabled') === 'true') event.preventDefault();
  });
  let playing = !reducedMotion.matches;
  let dirty = true;
  let radius = 4;
  let preset = 'game';
  let lastFrame = 0;
  let lastTimelineUpdate = 0;
  let contextLost = false;
  const target = new THREE.Vector3();
  const modelBounds = new THREE.Box3();
  const offset = new THREE.Vector3();
  const views = {
    game: new THREE.Vector3(1.3, 1, 1.35).normalize(),
    front: new THREE.Vector3(1, 0, 0),
    side: new THREE.Vector3(0, 0, 1),
    rear: new THREE.Vector3(-1, 0, 0),
    top: new THREE.Vector3(0, 1, 0.0001).normalize(),
  };

  function setCamera(name) {
    if (!model) return;
    preset = name;
    // Fit projected bounds rather than a sphere, retaining useful detail in wide layouts.
    const vertical = THREE.MathUtils.degToRad(camera.fov);
    const horizontal = 2 * Math.atan(Math.tan(vertical / 2) * camera.aspect);
    const direction = views[name];
    const right = new THREE.Vector3().crossVectors(new THREE.Vector3(0, 1, 0), direction).normalize();
    const up = new THREE.Vector3().crossVectors(direction, right).normalize();
    let distance = 0;
    for (const x of [modelBounds.min.x, modelBounds.max.x]) {
      for (const y of [modelBounds.min.y, modelBounds.max.y]) {
        for (const z of [modelBounds.min.z, modelBounds.max.z]) {
          offset.set(x, y, z).sub(target);
          const depth = offset.dot(direction);
          distance = Math.max(distance,
            depth + Math.abs(offset.dot(right)) / Math.tan(horizontal / 2),
            depth + Math.abs(offset.dot(up)) / Math.tan(vertical / 2));
        }
      }
    }
    distance *= 1.12;
    controls.target.copy(target);
    camera.up.set(0, 1, 0);
    camera.position.copy(target).addScaledVector(views[name], distance);
    controls.minDistance = radius * 0.65;
    controls.maxDistance = distance * 3;
    // Clear damping accumulated by the previous orbit before restoring the exact preset.
    const damping = controls.enableDamping;
    controls.enableDamping = false;
    controls.update();
    camera.position.copy(target).addScaledVector(views[name], distance);
    controls.target.copy(target);
    controls.update();
    controls.enableDamping = damping;
    $('camera-name').textContent = `${name.toUpperCase()} CAMERA`;
    cameraButtons.forEach((button) => button.setAttribute('aria-pressed', String(button.dataset.camera === name)));
    dirty = true;
  }

  function updatePlayButton() {
    $('play').textContent = playing ? 'Pause' : 'Play';
    $('play').setAttribute('aria-label', playing ? 'Pause animation' : 'Play animation');
    $('play').setAttribute('aria-pressed', String(playing));
    if (action) action.paused = !playing;
  }

  function updateTimeline() {
    if (!action) return;
    const duration = action.getClip().duration;
    $('timeline').value = action.time;
    $('timeline').setAttribute('aria-valuetext', `${action.time.toFixed(2)} of ${duration.toFixed(2)} seconds`);
    $('time-display').value = `${action.time.toFixed(2)} / ${duration.toFixed(2)} s`;
  }

  function selectAnimation(index) {
    mixer.stopAllAction();
    const clip = clips[index];
    action = mixer.clipAction(clip);
    action.reset();
    const once = !clipSpecs[index].loop;
    action.setLoop(once ? THREE.LoopOnce : THREE.LoopRepeat, once ? 1 : Infinity);
    action.clampWhenFinished = once;
    action.timeScale = Number($('speed').value);
    action.play();
    updatePlayButton();
    mixer.update(0);
    $('timeline').max = clip.duration;
    $('motion-note').textContent = `${once ? 'One-shot · holds final pose' : 'Looping mechanical study'}${reducedMotion.matches ? ' · motion paused by default' : ''}`;
    updateTimeline();
    dirty = true;
  }

  cameraButtons.forEach((button) => button.addEventListener('click', () => setCamera(button.dataset.camera)));
  $('reset-camera').addEventListener('click', () => setCamera('game'));
  controls.addEventListener('change', () => { dirty = true; });
  controls.addEventListener('start', () => {
    preset = null;
    $('camera-name').textContent = 'FREE CAMERA';
    cameraButtons.forEach((button) => button.setAttribute('aria-pressed', 'false'));
  });
  canvas.addEventListener('keydown', (event) => {
    if (!model || !['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', '+', '=', '-', '_', 'r', 'R'].includes(event.key)) return;
    event.preventDefault();
    if (event.key.toLowerCase() === 'r') { setCamera('game'); return; }
    preset = null;
    $('camera-name').textContent = 'FREE CAMERA';
    cameraButtons.forEach((button) => button.setAttribute('aria-pressed', 'false'));
    offset.copy(camera.position).sub(controls.target);
    const spherical = new THREE.Spherical().setFromVector3(offset);
    if (event.key === 'ArrowLeft') spherical.theta -= 0.12;
    if (event.key === 'ArrowRight') spherical.theta += 0.12;
    if (event.key === 'ArrowUp') spherical.phi -= 0.1;
    if (event.key === 'ArrowDown') spherical.phi += 0.1;
    if (event.key === '+' || event.key === '=') spherical.radius *= 0.88;
    if (event.key === '-' || event.key === '_') spherical.radius /= 0.88;
    spherical.phi = THREE.MathUtils.clamp(spherical.phi, 0.001, controls.maxPolarAngle);
    spherical.radius = THREE.MathUtils.clamp(spherical.radius, controls.minDistance, controls.maxDistance);
    camera.position.copy(controls.target).add(offset.setFromSpherical(spherical));
    controls.update();
    dirty = true;
  });
  $('animation').addEventListener('change', () => selectAnimation(Number($('animation').value)));
  $('play').addEventListener('click', () => {
    playing = !playing;
    if (playing && action.time >= action.getClip().duration) {
      action.reset().play();
      action.timeScale = Number($('speed').value);
    }
    updatePlayButton();
    dirty = true;
  });
  $('speed').addEventListener('change', () => { if (action) action.timeScale = Number($('speed').value); });
  $('timeline').addEventListener('input', () => {
    playing = false;
    action.enabled = true;
    action.time = Number($('timeline').value);
    updatePlayButton();
    mixer.update(0);
    updateTimeline();
    dirty = true;
  });
  $('team-colour').addEventListener('input', () => {
    teamMaterials.forEach((material) => material.color.set($('team-colour').value));
    $('colour-value').value = $('team-colour').value.toUpperCase();
    dirty = true;
  });
  $('wireframe').addEventListener('change', () => {
    materials.forEach((material) => { material.wireframe = $('wireframe').checked; });
    dirty = true;
  });
  $('lighting').addEventListener('change', () => {
    const dusk = $('lighting').value === 'dusk';
    hemisphere.intensity = dusk ? 0.55 : 1.8;
    hemisphere.color.set(dusk ? 0x8197d4 : 0xc8dfe9);
    key.color.set(dusk ? 0xffa866 : 0xffe6c8);
    key.intensity = dusk ? 2.4 : 3.7;
    fill.intensity = dusk ? 0.3 : 1.4;
    rim.intensity = dusk ? 1.8 : 3;
    scene.environmentIntensity = dusk ? 0.25 : 0.65;
    renderer.toneMappingExposure = dusk ? 0.95 : 1.2;
    viewport.style.background = dusk ? 'radial-gradient(ellipse at 50% 34%, #303344, #11191f 74%)' : '';
    dirty = true;
  });
  reducedMotion.addEventListener('change', () => {
    controls.enableDamping = !reducedMotion.matches;
    if (reducedMotion.matches) { playing = false; updatePlayButton(); }
  });

  const resize = new ResizeObserver(() => {
    const { width, height } = viewport.getBoundingClientRect();
    camera.aspect = width / Math.max(height, 1);
    camera.updateProjectionMatrix();
    renderer.setSize(width, height, false);
    if (preset) setCamera(preset);
    dirty = true;
  });
  resize.observe(viewport);
  canvas.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    contextLost = true;
    controls.enabled = false;
    showError('The browser lost its graphics context. Reload the viewer to restore the model.');
  });

  function disposeModel(root) {
    if (!root) return;
    const geometries = new Set();
    const surfaces = new Set();
    const textures = new Set();
    const skeletons = new Set();
    root.traverse((node) => {
      if (node.geometry) geometries.add(node.geometry);
      if (node.skeleton) skeletons.add(node.skeleton);
      if (node.material) (Array.isArray(node.material) ? node.material : [node.material]).forEach((surface) => {
        surfaces.add(surface);
        Object.values(surface).forEach((value) => { if (value?.isTexture) textures.add(value); });
      });
    });
    geometries.forEach((geometry) => geometry.dispose());
    skeletons.forEach((skeleton) => skeleton.dispose());
    textures.forEach((texture) => { texture.dispose(); texture.source?.data?.close?.(); });
    surfaces.forEach((surface) => surface.dispose());
  }

  function clearModel() {
    if (mixer) {
      mixer.stopAllAction();
      mixer.uncacheRoot(model);
    }
    if (model) { scene.remove(model); disposeModel(model); }
    model = mixer = action = undefined;
    clips = clipSpecs = materials = teamMaterials = [];
    controls.enabled = false;
    floor.visible = grid.visible = false;
    $('motion-controls').disabled = $('surface-controls').disabled = true;
    cameraButtons.forEach((button) => { button.disabled = true; });
    $('reset-camera').disabled = true;
    canvas.dataset.assetLoaded = 'false';
    download.removeAttribute('href');
    download.setAttribute('aria-disabled', 'true');
    download.tabIndex = -1;
    for (const id of ['dimensions', 'triangles', 'materials', 'clip-count']) $(id).textContent = '—';
    $('animation').replaceChildren(new Option('Loading clips…', ''));
    $('time-display').value = '0.00 / 0.00 s';
    $('timeline').value = 0;
    dirty = true;
  }

  function installModel(gltf, manifest, entry) {
    const specs = manifest.clips;
    if (!Array.isArray(specs) || !specs.length) throw new Error('Manifest contains no animation clips.');
    const matched = specs.map((spec) => {
      const matches = gltf.animations.filter((clip) => clip.name === spec.name || clip.name.endsWith(`_${spec.name}`));
      if (matches.length !== 1 || typeof spec.loop !== 'boolean'
          || Math.abs(matches[0].duration - spec.duration) > 0.08) {
        throw new Error(`Missing or mismatched animation: ${spec.name}`);
      }
      return matches[0];
    });
    if (matched.length !== gltf.animations.length) throw new Error('GLB has unexpected animation clips.');
    const materialSet = new Set();
    let triangles = 0;
    gltf.scene.traverse((node) => {
      if (!node.isMesh) return;
      node.castShadow = node.receiveShadow = true;
      const geometry = node.geometry;
      triangles += (geometry.index ? geometry.index.count : geometry.attributes.position.count) / 3 * (node.isInstancedMesh ? node.count : 1);
      (Array.isArray(node.material) ? node.material : [node.material]).forEach((material) => materialSet.add(material));
    });
    if (!triangles || Math.round(triangles) !== manifest.triangles) throw new Error('GLB geometry does not match the manifest.');
    gltf.scene.updateMatrixWorld(true);
    const bounds = modelBounds.setFromObject(gltf.scene, true);
    const dimensions = bounds.getSize(new THREE.Vector3());
    if (!Number.isFinite(dimensions.length()) || dimensions.length() <= 0) throw new Error('Invalid model bounds.');
    model = gltf.scene;
    materials = [...materialSet];
    teamMaterials = materials.filter((material) => /^Team(?:\.|$)/i.test(material.name));
    if (manifest.materials.Team && !teamMaterials.length) throw new Error('GLB is missing its recolourable Team slot.');
    materials.forEach((material) => { material.wireframe = $('wireframe').checked; });
    bounds.getCenter(target);
    radius = dimensions.length() / 2;
    camera.near = Math.max(0.01, radius / 100);
    camera.far = Math.max(500, radius * 100);
    camera.updateProjectionMatrix();
    floor.position.y = bounds.min.y - 0.025;
    grid.position.set(Math.round(target.x), bounds.min.y - 0.015, Math.round(target.z));
    floor.visible = grid.visible = true;
    key.position.copy(target).add(new THREE.Vector3(radius * 1.5, radius * 2.5, radius * 1.5));
    key.target.position.copy(target);
    key.shadow.camera.left = key.shadow.camera.bottom = -radius * 1.6;
    key.shadow.camera.right = key.shadow.camera.top = radius * 1.6;
    key.shadow.camera.near = 0.1;
    key.shadow.camera.far = radius * 8;
    key.shadow.camera.updateProjectionMatrix();
    key.shadow.needsUpdate = true;
    scene.add(model);
    $('dimensions').textContent = `${dimensions.x.toFixed(2)} × ${dimensions.z.toFixed(2)} × ${dimensions.y.toFixed(2)} m`;
    $('triangles').textContent = Math.round(triangles).toLocaleString('en-US');
    $('materials').textContent = materials.length;
    clips = matched;
    clipSpecs = specs;
    $('clip-count').textContent = clips.length;
    $('surface-controls').disabled = false;
    $('team-colour').disabled = teamMaterials.length === 0;
    if (teamMaterials.length) {
      $('team-colour').value = `#${teamMaterials[0].color.getHexString()}`;
      $('colour-value').value = $('team-colour').value.toUpperCase();
    } else {
      $('colour-value').value = 'No Team material';
    }
    $('animation').replaceChildren(...specs.map((spec, index) => new Option(spec.name, index)));
    mixer = new THREE.AnimationMixer(model);
    mixer.addEventListener('finished', () => { playing = false; updatePlayButton(); updateTimeline(); dirty = true; });
    playing = !reducedMotion.matches;
    const initial = Math.max(0, specs.findIndex((spec) => spec.name === 'Idle'));
    $('animation').value = initial;
    $('motion-controls').disabled = false;
    selectAnimation(initial);
    cameraButtons.forEach((button) => { button.disabled = false; });
    $('reset-camera').disabled = false;
    controls.enabled = true;
    setCamera('game');
    $('asset-title').textContent = manifest.name ?? entry.name;
    $('asset-category').textContent = `${manifest.faction ?? entry.faction} / ROSTER REVIEW`;
    $('asset-description').textContent = manifest.role ?? entry.role;
    $('model-status').textContent = `${manifest.faction ?? entry.faction} · ${manifest.role ?? entry.role} · ${manifest.status ?? entry.status}`;
    $('asset-revision').textContent = manifest.revision;
    document.title = `${manifest.name ?? entry.name} / Asset review — Post-Frontier`;
    canvas.setAttribute('aria-label', `3D ${manifest.name ?? entry.name} model. Drag to orbit, scroll or pinch to zoom. Arrow keys orbit, plus and minus zoom, R resets the camera.`);
    download.href = `${entry.glb}?v=${encodeURIComponent(manifest.revision)}`;
    download.download = `${entry.id}.glb`;
    download.removeAttribute('aria-disabled');
    download.tabIndex = 0;
    $('load-state').hidden = true;
    viewport.setAttribute('aria-busy', 'false');
    canvas.dataset.assetLoaded = 'true';
    $('scene-state').textContent = 'LIVE MODEL';
    dirty = true;
  }

  async function selectModel(id, updateUrl = true) {
    const token = ++selection;
    manifestRequest?.abort();
    manifestRequest = new AbortController();
    clearModel();
    $('load-state').hidden = false;
    $('load-state').classList.remove('error');
    $('retry').hidden = true;
    $('scene-state').textContent = 'LOADING ASSET';
    $('load-title').textContent = 'Preparing the review model';
    $('load-detail').textContent = 'Loading manifest, geometry and materials.';
    $('asset-title').textContent = 'Loading';
    $('asset-category').textContent = 'ROSTER / ART REVIEW';
    $('model-selector').value = id;
    document.title = 'Roster / Asset review — Post-Frontier';
    $('asset-description').textContent = '';
    $('asset-revision').textContent = '—';
    $('model-status').textContent = 'Loading model';
    viewport.setAttribute('aria-busy', 'true');
    let loaded;
    try {
      const entry = catalogue.find((item) => item.id === id);
      if (!entry) throw new Error(`Unknown catalogue model: ${id}`);
      if (updateUrl) {
        const url = new URL(window.location.href);
        url.searchParams.set('model', id);
        window.history.replaceState(null, '', url);
      }
      const response = await fetch(`${entry.manifest}?v=${encodeURIComponent(entry.revision)}`, { signal: manifestRequest.signal });
      if (!response.ok) throw new Error(`Manifest request failed (${response.status}): ${entry.id}`);
      const manifest = await response.json();
      if (token !== selection) return;
      if (manifest.revision !== entry.revision || (entry.id !== 'surveyor' && manifest.id !== entry.id)) {
        throw new Error('Catalogue and model manifest revisions do not match.');
      }
      loaded = await new GLTFLoader().loadAsync(`${entry.glb}?v=${encodeURIComponent(manifest.revision)}`, (event) => {
        if (token !== selection) return;
        $('load-detail').textContent = event.lengthComputable
          ? `Receiving model · ${Math.round(event.loaded / event.total * 100)}%`
          : `Receiving model · ${(event.loaded / 1024 / 1024).toFixed(1)} MB`;
      });
      if (token !== selection || contextLost) { disposeModel(loaded.scene); return; }
      installModel(loaded, manifest, entry);
    } catch (error) {
      if (token !== selection) { if (loaded) disposeModel(loaded.scene); return; }
      const attached = model === loaded?.scene;
      clearModel();
      if (loaded && !attached) disposeModel(loaded.scene);
      showError(error.message);
      console.error('Roster selection failed:', error);
    }
  }

  $('model-selector').addEventListener('change', () => selectModel($('model-selector').value));
  window.addEventListener('popstate', () => selectModel(new URLSearchParams(window.location.search).get('model') ?? 'surveyor', false));
  clearModel();
  async function loadCatalogue() {
    try {
      const response = await fetch(`./catalogue.json?v=${encodeURIComponent(new URL(import.meta.url).searchParams.get('v') ?? '')}`);
      if (!response.ok) throw new Error(`Catalogue request failed (${response.status}).`);
      catalogue = await response.json();
      if (!Array.isArray(catalogue) || !catalogue.length || new Set(catalogue.map((entry) => entry.id)).size !== catalogue.length) {
        throw new Error('Invalid review catalogue.');
      }
      const groups = new Map();
      for (const entry of catalogue) {
        const key = entry.id === 'surveyor' ? 'Original art candidate' : entry.faction;
        if (!groups.has(key)) {
          const group = document.createElement('optgroup');
          group.label = key;
          groups.set(key, group);
        }
        groups.get(key).append(new Option(`${entry.name} — ${entry.role} (${entry.status})`, entry.id));
      }
      $('model-selector').replaceChildren(...groups.values());
      $('model-selector').disabled = false;
      await selectModel(new URLSearchParams(window.location.search).get('model') ?? 'surveyor');
    } catch (error) {
      showError(error.message);
      console.error('Roster catalogue failed:', error);
    }
  }
  loadCatalogue();

  function frame(now) {
    if (contextLost) return;
    requestAnimationFrame(frame);
    const delta = lastFrame ? Math.min((now - lastFrame) / 1000, 0.1) : 0;
    lastFrame = now;
    if (document.hidden) return;
    if (mixer && playing) {
      mixer.update(delta);
      dirty = true;
      if (now - lastTimelineUpdate > 50) { updateTimeline(); lastTimelineUpdate = now; }
    }
    if (controls.update()) dirty = true;
    if (dirty) { renderer.render(scene, camera); dirty = false; }
  }
  document.addEventListener('visibilitychange', () => { lastFrame = 0; dirty = true; });
  requestAnimationFrame(frame);
}
