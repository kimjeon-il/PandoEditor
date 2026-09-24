/* Shared by the map-edit Worker and the application commit guard. No state writes. */
(function (root) {
  const id = value => String(value ?? '');
  const clone = value => structuredClone(value);
  const coordinates = geometry => geometry?.type === 'Polygon' ? [geometry.coordinates] : geometry?.coordinates || [];
  const area = geometry => coordinates(geometry).reduce((total, polygon) => total + polygon.reduce((sum, ring, index) => {
    let signed = 0;
    for (let i = 1; i < ring.length; i++) signed += ring[i - 1][0] * ring[i][1] - ring[i][0] * ring[i - 1][1];
    return sum + (index ? -1 : 1) * Math.abs(signed / 2);
  }, 0), 0);
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

  function createKernel(clipper, { normalize = geometry => geometry, segmentCandidates = null } = {}) {
    const boundsCache = new WeakMap();
    const bounds = geometry => {
      if (!geometry) return [Infinity, Infinity, -Infinity, -Infinity];
      if (boundsCache.has(geometry)) return boundsCache.get(geometry);
      const result = [Infinity, Infinity, -Infinity, -Infinity];
      for (const polygon of coordinates(geometry)) for (const ring of polygon) for (const [x, y] of ring) {
        result[0] = Math.min(result[0], x); result[1] = Math.min(result[1], y);
        result[2] = Math.max(result[2], x); result[3] = Math.max(result[3], y);
      }
      boundsCache.set(geometry, result);
      return result;
    };
    const disjoint = (a, b) => {
      const left = bounds(a), right = bounds(b);
      return left[2] < right[0] || right[2] < left[0] || left[3] < right[1] || right[3] < left[1];
    };
    const shape = value => value?.length ? { type: 'MultiPolygon', coordinates: value } : null;
    const intersection = (a, b) => a && b && !disjoint(a, b) ? shape(clipper.intersection(coordinates(a), coordinates(b))) : null;
    const difference = (a, b) => a && b && !disjoint(a, b) ? shape(clipper.difference(coordinates(a), coordinates(b))) : clone(a);
    const union = (...values) => {
      const valid = values.filter(Boolean);
      return valid.length ? shape(clipper.union(...valid.map(coordinates))) : null;
    };
    const significant = (geometry, reference) => area(geometry) > Math.max(1e-10, area(reference) * 1e-9);
    const contains = (parent, child) => !significant(difference(child, parent), child);
    function adjacent(a, b) {
      function* allSegments(geometry) {
        for (const polygon of coordinates(geometry)) for (const ring of polygon) for (let index = 1; index < ring.length; index++) yield { a: ring[index - 1], b: ring[index] };
      }
      // Collinear overlap works when neighboring rings have different segmentation.
      for (const pa of coordinates(a)) for (const ra of pa) for (let i = 1; i < ra.length; i++) {
        const p = ra[i - 1], q = ra[i], dx = q[0] - p[0], dy = q[1] - p[1];
        const length = Math.hypot(dx, dy);
        if (!length) continue;
        for (const { a: u, b: v } of segmentCandidates ? segmentCandidates(b, p, q) : allSegments(b)) {
          if (Math.abs(dx * (u[1] - p[1]) - dy * (u[0] - p[0])) / length > 1e-7
            || Math.abs(dx * (v[1] - p[1]) - dy * (v[0] - p[0])) / length > 1e-7) continue;
          const t = r => ((r[0] - p[0]) * dx + (r[1] - p[1]) * dy) / (length * length);
          if (Math.min(1, Math.max(t(u), t(v))) - Math.max(0, Math.min(t(u), t(v))) > 1e-8) return true;
        }
      }
      return false;
    }

    function validate(countries, units, previous = [], affectedIds = null) {
      const all = new Map([...countries, ...units].map(feature => [id(feature.id), feature]));
      const old = new Map(previous.map(feature => [id(feature.id), feature]));
      const affected = affectedIds && new Set(affectedIds.map(id));
      for (const unit of units) {
        if (unit.properties?.unitType !== 'subunit') continue;
        const key = id(unit.id), parent = all.get(id(unit.properties.parentId));
        if (!parent || id(parent.id) === key) throw new Error(`${key}: 상위 단위를 찾을 수 없습니다.`);
        const country = id(unit.properties.sovereignId);
        if (!countries.some(item => id(item.id) === country)
          || (id(parent.id) !== country && (parent.properties?.unitType !== 'subunit' || id(parent.properties.sovereignId) !== country))) {
          throw new Error(`${key}: 다른 소속 국가의 상위 단위를 지정할 수 없습니다.`);
        }
        const seen = new Set([key]);
        let cursor = parent;
        while (cursor?.properties?.unitType === 'subunit') {
          if (seen.has(id(cursor.id))) throw new Error(`${key}: 상위 단위 관계가 순환합니다.`);
          seen.add(id(cursor.id)); cursor = all.get(id(cursor.properties.parentId));
        }
        if (affected && !affected.has(key) && !affected.has(id(parent.id))) continue;
        if (!unit.geometry || area(unit.geometry) <= 0 || !contains(parent.geometry, unit.geometry)) throw new Error(`${key}: 형상이 상위 단위 밖으로 벗어났습니다.`);
        for (const sibling of units) {
          if (id(sibling.id) === key || sibling.properties?.unitType !== 'subunit'
            || id(sibling.properties.parentId) !== id(unit.properties.parentId)) continue;
          if (significant(intersection(unit.geometry, sibling.geometry), unit.geometry)) throw new Error(`${key}: 형제 ${sibling.id}와 영역이 겹칩니다.`);
        }
      }
      for (const [key, before] of old) if (before.properties?.locked && !same(before, all.get(key))) throw new Error(`${key}: 잠긴 객체는 변경할 수 없습니다.`);
      return true;
    }

    function createDraft(request) {
      // Inputs are read-only; only put()/explicit patches own mutable copies.
      const countries = [...(request.countries || [])], units = [...(request.units || [])];
      const before = [...countries, ...units], byId = new Map(before.map(feature => [id(feature.id), feature]));
      const patches = new Map(), removed = new Set(), impacts = [], ownershipChanges = [];
      const read = key => patches.get(id(key)) || byId.get(id(key));
      const children = key => units.filter(unit => unit.properties?.unitType === 'subunit' && id(unit.properties.parentId) === id(key));
      function put(feature, geometry, parentId) {
        if (!feature) throw new Error('변경할 객체를 찾을 수 없습니다.');
        const next = clone(read(feature.id));
        if (!geometry || !area(geometry)) { removed.add(id(feature.id)); patches.delete(id(feature.id)); return; }
        next.geometry = geometry;
        if (parentId != null && id(next.properties.parentId) !== id(parentId)) {
          ownershipChanges.push({ id: id(feature.id), from: id(next.properties.parentId), to: id(parentId) });
          next.properties.parentId = id(parentId);
        }
        if (!same(next, byId.get(id(feature.id)))) patches.set(id(feature.id), next);
      }
      function reconcileChildren(parent, receiver = null) {
        for (const child of children(parent.id)) {
          const nextParent = removed.has(id(parent.id)) ? null : read(parent.id);
          if (receiver && contains(read(receiver.id).geometry, child.geometry)) {
            put(child, clone(child.geometry), receiver.id);
            continue;
          }
          const kept = intersection(child.geometry, nextParent?.geometry);
          const cut = difference(child.geometry, kept);
          if (!significant(cut, child.geometry)) continue;
          impacts.push({ kind: kept ? 'clip-child' : 'remove-child', id: id(child.id), name: child.properties.name || id(child.id), area: area(cut), geometry: cut });
          put(child, kept);
          reconcileChildren(child, receiver);
        }
      }
      const target = byId.get(id(request.targetId));
      if (!target) throw new Error('편집 대상을 찾을 수 없습니다.');
      if (target.properties?.locked) throw new Error('잠긴 객체는 편집할 수 없습니다.');
      return { countries, units, before, byId, patches, removed, impacts, ownershipChanges, read, children, put, reconcileChildren, target };
    }

    function siblingScope(request, { byId, units }) {
      const parentId = id(request.parentId), parent = byId.get(parentId);
      if (!parent) throw new Error('허용 상위 범위를 찾을 수 없습니다.');
      if (parent.properties?.locked) throw new Error('상위 단위의 잠금을 해제하세요.');
      const countryId = parent.properties?.unitType === 'subunit' ? id(parent.properties.sovereignId) : id(parent.id);
      const siblings = units.filter(unit => unit.properties?.unitType === 'subunit'
        && id(unit.properties.parentId) === parentId && id(unit.properties.sovereignId) === countryId);
      const allowed = new Set(siblings.map(unit => id(unit.id)));
      const assertSibling = unit => { if (!unit || !allowed.has(id(unit.id))) throw new Error('같은 소속 국가·상위 단위 안에서만 편집할 수 있습니다.'); };
      return { parentId, parent, countryId, siblings, assertSibling };
    }

    function planCountryBoundary(request) {
      const draft = createDraft(request);
      const { countries, byId, patches, ownershipChanges, children, put, impacts } = draft;
      const countryIds = new Set(countries.map(country => id(country.id)));
      const edits = (request.featurePatches || []).map(feature => ({ before: byId.get(id(feature.id)), after: feature }));
      if (edits.length < 2 || edits.some(edit => !countryIds.has(id(edit.before?.id)))) throw new Error('공유 국경 양쪽 국가를 선택하세요.');
      const original = union(...edits.map(edit => edit.before.geometry));
      const proposed = union(...edits.map(edit => edit.after.geometry));
      if (!contains(original, proposed) || !contains(proposed, original)) throw new Error('국경 조정으로 선택 국가의 바깥 경계를 변경할 수 없습니다.');
      for (const edit of edits) {
        if (!edit.after.geometry || !area(edit.after.geometry)) throw new Error('국가 전체를 없애는 국경 조정은 허용하지 않습니다.');
        for (const other of countries) {
          if (id(other.id) === id(edit.before.id)) continue;
          const nextOther = edits.find(item => id(item.before.id) === id(other.id))?.after || other;
          const gained = difference(edit.after.geometry, edit.before.geometry);
          if (significant(intersection(gained, nextOther.geometry), gained)) throw new Error('다른 국가의 영토를 중복 소유할 수 없습니다.');
        }
        put(edit.before, clone(edit.after.geometry));
      }
      function moveHierarchy(unit, countryId, parentId = unit.properties.parentId) {
        const next = clone(unit);
        next.properties.sovereignId = countryId; next.properties.parentId = parentId;
        patches.set(id(unit.id), next);
        ownershipChanges.push({ id: id(unit.id), from: id(unit.properties.parentId), to: id(parentId), sovereignId: countryId });
        children(unit.id).forEach(child => moveHierarchy(child, countryId));
      }
      function reconcile(parentId, geometry, sourceCountryId) {
        for (const child of children(parentId)) {
          const destination = edits.find(edit => id(edit.before.id) !== sourceCountryId && contains(edit.after.geometry, child.geometry));
          if (destination) { moveHierarchy(child, id(destination.before.id), id(destination.before.id)); continue; }
          const kept = intersection(child.geometry, geometry), cut = difference(child.geometry, kept);
          if (significant(cut, child.geometry)) {
            impacts.push({ kind: kept ? 'clip-child' : 'remove-child', id: id(child.id), name: child.properties.name || id(child.id), geometry: cut });
            put(child, kept);
          }
          reconcile(child.id, kept, sourceCountryId);
        }
      }
      for (const edit of edits) reconcile(edit.before.id, edit.after.geometry, id(edit.before.id));
      return draft;
    }

    function planTransfer(request) {
      const draft = createDraft(request);
      const { countries, units, byId, target, patches, ownershipChanges, children, put, impacts } = draft;
      const oldCountry = byId.get(id(target.properties.sovereignId));
      const promoting = request.operation === 'promote';
      const newCountry = promoting ? clone(request.newCountry) : countries.find(country => id(country.id) === id(request.countryId));
      if (target.properties.unitType !== 'subunit' || !oldCountry || !newCountry || id(oldCountry.id) === id(newCountry.id)) throw new Error('이전할 소속 국가를 선택하세요.');
      if (promoting && (id(newCountry.id) !== id(target.id) || countries.some(country => id(country.id) === id(newCountry.id)))) throw new Error('새 국가 ID가 올바르지 않습니다.');
      const kept = difference(oldCountry.geometry, target.geometry);
      if (!significant(kept, oldCountry.geometry)) throw new Error('기존 국가의 영역 전체를 이전할 수 없습니다.');
      put(oldCountry, kept);
      if (promoting) {
        newCountry.geometry = clone(target.geometry);
        countries.push(newCountry);
        patches.set(id(newCountry.id), newCountry);
      } else put(newCountry, union(newCountry.geometry, target.geometry));
      const moved = new Set();
      function move(unit) {
        moved.add(id(unit.id));
        const next = clone(unit); next.properties.sovereignId = id(newCountry.id);
        if (id(unit.id) === id(target.id)) next.properties.parentId = id(newCountry.id);
        patches.set(id(unit.id), next);
        ownershipChanges.push({ id: id(unit.id), from: id(unit.properties.parentId), to: id(next.properties.parentId), sovereignId: id(newCountry.id) });
        children(unit.id).forEach(move);
      }
      if (promoting) { moved.add(id(target.id)); children(target.id).forEach(move); }
      else move(target);
      for (const unit of units) {
        if (unit.properties.unitType !== 'subunit' || id(unit.properties.sovereignId) !== id(oldCountry.id) || moved.has(id(unit.id))) continue;
        if (!significant(intersection(unit.geometry, target.geometry), unit.geometry)) continue;
        const remaining = difference(unit.geometry, target.geometry);
        put(unit, remaining);
        if (!remaining) impacts.push({ kind: 'remove-child', id: id(unit.id), name: unit.properties.name || id(unit.id) });
      }
      return draft;
    }

    function planCoast(request) {
      const draft = createDraft(request);
      const { countries, units, byId, target, removed, read, children, put, impacts } = draft;
      const countryId = target.properties?.unitType === 'subunit' ? id(target.properties.sovereignId) : id(target.id);
      const country = byId.get(countryId);
      if (!country || !request.draft) throw new Error('변경할 국가 해안선을 찾을 수 없습니다.');
      // draft always describes the country result, irrespective of entrypoint.
      const baseline = request.coastBaseline || country.geometry;
      const added = difference(request.draft, baseline), lost = difference(baseline, request.draft);
      for (const other of countries) if (id(other.id) !== countryId && significant(intersection(added, other.geometry), added)) throw new Error('다른 국가의 기존 영토를 침범할 수 없습니다.');
      put(country, clone(request.draft));
      for (const unit of units.filter(unit => unit.properties?.unitType === 'subunit' && id(unit.properties.sovereignId) === countryId)) {
        const kept = difference(unit.geometry, lost);
        const cut = intersection(unit.geometry, lost);
        if (significant(cut, unit.geometry)) {
          put(unit, kept);
          if (kept) impacts.push({ kind: 'clip-child', id: id(unit.id), name: unit.properties.name || id(unit.id), area: area(cut), geometry: cut });
        }
      }
      function allocate(parent, addition, path) {
        const candidates = children(parent.id).filter(child => adjacent(child.geometry, addition));
        if (!candidates.length) return;
        const selectedId = request.allocations?.[path];
        const selected = candidates.length === 1 ? candidates[0] : candidates.find(child => id(child.id) === id(selectedId));
        if (!selected) {
          impacts.push({ kind: 'coast-owner', key: path, parentId: id(parent.id), geometry: addition,
            candidates: candidates.map(child => ({ id: id(child.id), name: child.properties.name || id(child.id) })) });
          return;
        }
        removed.delete(id(selected.id));
        put(selected, union(difference(read(selected.id).geometry, lost), addition));
        allocate(selected, addition, `${path}/${selected.id}`);
      }
      coordinates(added).forEach((polygon, index) => allocate(country, shape([polygon]), `${countryId}/${index}`));
      for (const key of removed) impacts.push({ kind: 'remove-child', id: key, name: byId.get(key)?.properties?.name || key });
      return draft;
    }

    function planMerge(request) {
      const draft = createDraft(request);
      const { byId, target, removed, ownershipChanges, children, put } = draft;
      const { assertSibling } = siblingScope(request, draft);
      assertSibling(target);
      const targets = [...new Set((request.sourceIds || []).map(id))].filter(key => key !== id(target.id)).map(key => byId.get(key));
      targets.forEach(assertSibling);
      if (!targets.length) throw new Error('합병 대상을 선택하세요.');
      const connected = [target], pending = [...targets];
      while (pending.length) {
        const index = pending.findIndex(candidate => connected.some(other => adjacent(candidate.geometry, other.geometry)));
        if (index < 0) throw new Error('선택한 하위단위들이 서로 연결되어야 합니다.');
        connected.push(...pending.splice(index, 1));
      }
      put(target, union(...connected.map(unit => unit.geometry)));
      for (const donor of targets) {
        removed.add(id(donor.id));
        ownershipChanges.push({ id: id(donor.id), replacementId: id(target.id) });
        for (const child of children(donor.id)) put(child, clone(child.geometry), target.id);
      }
      return draft;
    }

    function planBoundary(request) {
      const draft = createDraft(request);
      const { byId, target, read, children, put, impacts, reconcileChildren } = draft;
      const { assertSibling } = siblingScope(request, draft);
      assertSibling(target);
      const edits = (request.featurePatches || []).map(feature => ({ before: byId.get(id(feature.id)), after: feature }));
      if (edits.length < 2) throw new Error('공유 경계 양쪽을 함께 변경해야 합니다.');
      edits.forEach(edit => assertSibling(edit.before));
      const original = union(...edits.map(edit => edit.before.geometry));
      const proposed = union(...edits.map(edit => edit.after.geometry));
      if (!contains(original, proposed) || !contains(proposed, original)) throw new Error('공유 경계 조정으로 바깥 경계를 변경할 수 없습니다.');
      for (const edit of edits) put(edit.before, clone(edit.after.geometry));
      for (const edit of edits) {
        for (const child of children(edit.before.id)) {
          const receiver = edits.find(other => id(other.before.id) !== id(edit.before.id) && contains(other.after.geometry, child.geometry));
          if (receiver) put(child, clone(child.geometry), receiver.before.id);
        }
        // Fully transferred children have already acquired their destination.
        for (const child of children(edit.before.id)) {
          if (id(read(child.id).properties.parentId) !== id(edit.before.id)) continue;
          const kept = intersection(child.geometry, edit.after.geometry);
          const cut = difference(child.geometry, kept);
          if (!significant(cut, child.geometry)) continue;
          impacts.push({ kind: kept ? 'clip-child' : 'remove-child', id: id(child.id), name: child.properties.name || id(child.id), area: area(cut), geometry: cut });
          put(child, kept); reconcileChildren(child);
        }
      }
      return draft;
    }

    function planCreateAnnex(request) {
      const draft = createDraft(request);
      const { byId, target, patches, put, reconcileChildren } = draft;
      const { parentId, parent, countryId, siblings, assertSibling } = siblingScope(request, draft);
      if (request.operation === 'annex') assertSibling(target);
      const donor = request.sourceId ? byId.get(id(request.sourceId)) : null;
      if (request.sourceId && !donor) throw new Error('기준 영역을 찾을 수 없습니다.');
      if (donor) assertSibling(donor);
      if (donor && id(donor.id) === id(target.id) && request.operation === 'annex') throw new Error('자기 영역을 편입할 수 없습니다.');
      const sourceGeometry = donor?.geometry || difference(parent.geometry, union(...siblings.map(unit => unit.geometry)));
      if (!request.draft || !contains(sourceGeometry, request.draft)) throw new Error('기준 영역 밖으로 벗어났습니다.');
      const kept = difference(sourceGeometry, request.draft);
      if (donor && request.operation === 'create' && !significant(kept, sourceGeometry)) throw new Error('기존 하위단위 전체를 새 객체로 대체할 수 없습니다.');
      let receiver = target;
      if (request.operation === 'create') {
        receiver = clone(request.newFeature);
        if (!receiver || !id(receiver.id) || receiver.properties?.unitType !== 'subunit' || byId.has(id(receiver.id))) throw new Error('새 하위단위 ID가 올바르지 않습니다.');
        receiver.properties.parentId = parentId; receiver.properties.sovereignId = countryId;
        receiver.geometry = clone(request.draft); patches.set(id(receiver.id), receiver);
      } else put(target, union(target.geometry, request.draft));
      if (donor) { put(donor, kept); reconcileChildren(donor, receiver); }
      return draft;
    }

    function finalizeDraft({ countries, before, byId, patches, removed, impacts, ownershipChanges }) {
      const features = [...patches.values()], removedIds = [...removed];
      for (const key of removedIds) if (!impacts.some(impact => impact.kind === 'remove-child' && impact.id === key)
        && !ownershipChanges.some(change => change.id === key && change.replacementId)) {
        impacts.push({ kind: 'remove-child', id: key, name: byId.get(key)?.properties?.name || key });
      }
      for (const feature of features) feature.geometry = normalize(feature.geometry);
      const all = before.filter(feature => !removed.has(id(feature.id)) && !patches.has(id(feature.id))).concat(features);
      const countryIds = new Set(countries.map(feature => id(feature.id)));
      const nextCountries = all.filter(feature => countryIds.has(id(feature.id)));
      const nextUnits = all.filter(feature => !countryIds.has(id(feature.id)));
      if (!impacts.some(impact => impact.kind === 'coast-owner')) validate(nextCountries, nextUnits, before, [...patches.keys(), ...removed]);
      return { features, removedIds, countryIds: [...countryIds], affectedIds: [...new Set([...patches.keys(), ...removed])], ownershipChanges, impacts };
    }
    function plan(request) {
      const strategy = {
        'country-boundary': planCountryBoundary, promote: planTransfer, transfer: planTransfer,
        coast: planCoast, merge: planMerge, boundary: planBoundary, create: planCreateAnnex, annex: planCreateAnnex,
      }[request.operation];
      if (!strategy) throw new Error('지원하지 않는 영역 편집 작업입니다.');
      // Every strategy owns its draft. Validate normalized results against the original snapshot.
      return finalizeDraft(strategy(request));
    }
    return Object.freeze({ plan, validate, adjacent, area, contains });
  }
  root.PandoLabTerritorialEdit = Object.freeze({ createKernel });
})(globalThis);
