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
    const shutdown = clip.name.toLowerCase() === 'shutdown';
    action.setLoop(shutdown ? THREE.LoopOnce : THREE.LoopRepeat, shutdown ? 1 : Infinity);
    action.clampWhenFinished = shutdown;
    action.timeScale = Number($('speed').value);
    action.play();
    updatePlayButton();
    mixer.update(0);
    $('timeline').max = clip.duration;
    $('motion-note').textContent = `${shutdown ? 'One-shot · holds final pose' : 'Looping mechanical study'}${reducedMotion.matches ? ' · motion paused by default' : ''}`;
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

  new GLTFLoader().load(new URL('../Surveyor/surveyor.glb', document.baseURI).href, (gltf) => {
    try {
      model = gltf.scene;
      const materialSet = new Set();
      let triangles = 0;
      model.traverse((node) => {
        if (!node.isMesh) return;
        node.castShadow = true;
        node.receiveShadow = true;
        const geometry = node.geometry;
        triangles += (geometry.index ? geometry.index.count : geometry.attributes.position.count) / 3 * (node.isInstancedMesh ? node.count : 1);
        (Array.isArray(node.material) ? node.material : [node.material]).forEach((material) => materialSet.add(material));
      });
      if (!triangles) throw new Error('The GLB contains no renderable mesh geometry.');
      materials = [...materialSet];
      teamMaterials = materials.filter((material) => /^Team(?:\.|$)/i.test(material.name));
      model.updateMatrixWorld(true);
      const bounds = modelBounds.setFromObject(model, true);
      const dimensions = bounds.getSize(new THREE.Vector3());
      bounds.getCenter(target);
      radius = dimensions.length() / 2;
      floor.position.y = bounds.min.y - 0.025;
      grid.position.y = bounds.min.y - 0.015;
      grid.position.x = Math.round(target.x);
      grid.position.z = Math.round(target.z);
      key.target.position.copy(target);
      key.shadow.camera.left = key.shadow.camera.bottom = -radius * 1.6;
      key.shadow.camera.right = key.shadow.camera.top = radius * 1.6;
      key.shadow.camera.near = 0.1;
      key.shadow.camera.far = 40;
      key.shadow.camera.updateProjectionMatrix();
      scene.add(model);
      $('dimensions').textContent = `${dimensions.x.toFixed(2)} × ${dimensions.z.toFixed(2)} × ${dimensions.y.toFixed(2)} m`;
      $('triangles').textContent = Math.round(triangles).toLocaleString('en-US');
      $('materials').textContent = materials.length;
      clips = gltf.animations;
      $('clip-count').textContent = clips.length;
      $('surface-controls').disabled = false;
      $('team-colour').disabled = teamMaterials.length === 0;
      if (teamMaterials.length) {
        $('team-colour').value = `#${teamMaterials[0].color.getHexString()}`;
        $('colour-value').value = $('team-colour').value.toUpperCase();
      } else {
        $('colour-value').value = 'No Team material';
      }
      $('animation').replaceChildren(...clips.map((clip, index) => new Option(clip.name || `Clip ${index + 1}`, index)));
      if (clips.length) {
        mixer = new THREE.AnimationMixer(model);
        mixer.addEventListener('finished', () => { playing = false; updatePlayButton(); updateTimeline(); dirty = true; });
        const initial = Math.max(0, clips.findIndex((clip) => clip.name === 'Idle'));
        $('animation').value = initial;
        $('motion-controls').disabled = false;
        selectAnimation(initial);
      } else {
        $('animation').add(new Option('No embedded animation', ''));
        $('motion-note').textContent = 'This asset contains no animation clips.';
      }
      cameraButtons.forEach((button) => { button.disabled = false; });
      $('reset-camera').disabled = false;
      controls.enabled = true;
      setCamera('game');
      $('load-state').hidden = true;
      viewport.setAttribute('aria-busy', 'false');
      canvas.dataset.assetLoaded = 'true';
      $('scene-state').textContent = 'LIVE MODEL';
      dirty = true;
    } catch (error) {
      showError(error.message);
      console.error(error);
    }
  }, (event) => {
    $('load-detail').textContent = event.lengthComputable
      ? `Receiving model · ${Math.round(event.loaded / event.total * 100)}%`
      : `Receiving model · ${(event.loaded / 1024 / 1024).toFixed(1)} MB`;
  }, (error) => {
    showError('The Surveyor GLB could not be loaded. Check your connection and reload. The published review must include Surveyor/surveyor.glb.');
    console.error('Surveyor GLB load failed:', error);
  });

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
