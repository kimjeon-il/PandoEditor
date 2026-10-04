import { runProjectTransaction } from './project-transaction.js';

export async function runMapEditTransaction({
  client,
  operation,
  payload,
  snapshot,
  applyResult,
  validateCanonical = () => true,
  commitHistory,
  restore,
  queueAutosave,
  onSuccess = () => {},
  onError = () => {},
  diagnostic = null,
}) {
  let requestId = 0;
  let workerCommitted = false;

  return runProjectTransaction({
    operationType: operation,
    snapshot,
    diagnostic,
    prepare: async () => {
      const response = await client.execute(operation, payload);
      requestId = response.requestId;
      return response.result;
    },
    applyCanonical: applyResult,
    validateCanonical,
    commitExternal: () => {
      client.commit(requestId);
      workerCommitted = true;
    },
    commitHistory: () => commitHistory(snapshot),
    queueAutosave,
    restore: editableSnapshot => {
      if (requestId && !workerCommitted) client.discard(requestId);
      restore(editableSnapshot, { rebaseWorker: workerCommitted });
    },
    onSuccess,
    onError,
  });
}
