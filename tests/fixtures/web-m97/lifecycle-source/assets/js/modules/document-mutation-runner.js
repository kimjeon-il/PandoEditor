export function createDocumentMutationRunner({ commandPipeline } = {}) {
  if (typeof commandPipeline?.runMutation !== 'function') {
    throw new TypeError('A commandPipeline with runMutation() is required.');
  }
  return (meta, mutate, options = {}) => {
    const result = commandPipeline.runMutation(meta, mutate, options);
    if (!result?.ok) throw result?.error || new Error('Document mutation failed.');
    return result.value;
  };
}
